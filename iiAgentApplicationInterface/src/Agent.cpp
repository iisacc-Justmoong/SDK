#include "Agent.h"
#include "SourceProcedure.h"

#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace iiAgentApplicationInterface {

/// Private host registrations and ordered, reentrant-safe event dispatch state.
struct Runtime::Impl {
    /// A command's callbacks and immutable metadata, identified across replacement.
    struct Entry { Capability capability; Validator validator; Handler handler; std::uint64_t generation; };
    /// Shared cross-thread handoff, invalidated before the runtime implementation is destroyed.
    struct TrackedBridge {
        std::mutex mutex;
        Runtime* runtime = nullptr;
        std::thread::id owner = std::this_thread::get_id();
        LifecycleDispatcher dispatcher;
        std::deque<std::pair<std::string, std::uint64_t>> pending;
    };
    /// Host-owned native object referenced weakly with its exact static C++ type.
    struct LiveBinding { std::weak_ptr<void> object; std::type_index type; };
    /// A callback and identity owned until explicit unsubscribe or runtime destruction.
    struct Subscriber { AgentContext context; Observer observer; };
    /// An event with non-public authorization information captured at publication time.
    struct Pending { Event event; std::vector<std::string> permissions; std::vector<Subscription> recipients; };

    explicit Impl(Authorizer policy) : authorizer(std::move(policy)) {}

    /// Fail closed if no host policy exists or if it raises an exception.
    bool allow(const AgentContext& context, Action action, const std::string& object,
               const std::string& capability = {}, const std::vector<std::string>& permissions = {}) const {
        if (!authorizer) return false;
        try { return authorizer({context, action, object, capability, permissions}); }
        catch (...) { return false; }
    }

    /// Queue a complete change before invoking observers, preserving event order.
    void publish_batch(std::vector<Pending> batch) {
        std::vector<Subscription> recipients;
        recipients.reserve(observers.size());
        for (const auto& [id, subscriber] : observers) { (void)subscriber; recipients.push_back(id); }
        for (auto& item : batch) {
            item.recipients = recipients;
            pending.push_back(std::move(item));
        }
        if (dispatching) return;
        dispatching = true;
        try {
            while (!pending.empty()) {
                Pending current = std::move(pending.front());
                pending.pop_front();
                for (const auto id : current.recipients) {
                    auto found = observers.find(id);
                    if (found == observers.end()) continue;
                    const auto subscriber = found->second;
                    if (!allow(subscriber.context, Action::observe, current.event.object_id,
                               current.event.capability_id, current.permissions)) continue;
                    try { subscriber.observer(current.event); } catch (...) { /* isolate host observers */ }
                }
            }
        } catch (...) {
            dispatching = false;
            throw;
        }
        dispatching = false;
    }

    /// Publish one event through the same ordered, lifetime-aware delivery path.
    void publish(Event event, std::vector<std::string> permissions = {}) {
        std::vector<Pending> batch;
        batch.push_back({std::move(event), std::move(permissions), {}});
        publish_batch(std::move(batch));
    }

    Authorizer authorizer;
    std::map<std::string, Object, std::less<>> objects;
    std::map<std::string, LiveBinding, std::less<>> live_objects;
    std::map<std::pair<std::string, std::string>, Entry> commands;
    std::map<Subscription, Subscriber> observers;
    std::deque<Pending> pending;
    std::shared_ptr<TrackedBridge> tracked_bridge = std::make_shared<TrackedBridge>();
    std::uint64_t next_instance = 1;
    std::uint64_t next_tracked_id = 1;
    std::uint64_t next_generation = 1;
    Subscription next_subscription = 1;
    bool dispatching = false;
};

/// Construct the registry with a host policy; an empty policy denies all access.
Runtime::Runtime(Authorizer authorizer) : impl_(std::make_unique<Impl>(std::move(authorizer))) {
    impl_->tracked_bridge->runtime = this;
}
/// Invalidate object destruction callbacks before releasing registrations and subscriptions.
Runtime::~Runtime() {
    std::lock_guard lock(impl_->tracked_bridge->mutex);
    impl_->tracked_bridge->runtime = nullptr;
    impl_->tracked_bridge->dispatcher = {};
}

/// Check affinity without dispatching work or accessing application state.
bool Runtime::is_owner_thread() const noexcept {
    return impl_->tracked_bridge->owner == std::this_thread::get_id();
}

/// Assign a fresh identity and publish the creation after registration succeeds.
Status Runtime::add_object(Object object) {
    if (object.id.empty() || object.type.empty()) return Status::invalid_argument;
    if (impl_->objects.contains(object.id)) return Status::already_exists;
    object.instance = impl_->next_instance++;
    object.revision = 1;
    const auto id = object.id;
    const auto instance = object.instance;
    impl_->objects.emplace(id, std::move(object));
    impl_->publish({EventKind::object_added, id, {}, instance, 1});
    return Status::ok;
}

/// Remove an object and its commands, then publish the removal batch in order.
Status Runtime::remove_object(std::string_view object_id) {
    auto object = impl_->objects.find(object_id);
    if (object == impl_->objects.end()) return Status::not_found;
    const auto snapshot = object->second;
    std::vector<Impl::Pending> removals;
    for (auto entry = impl_->commands.begin(); entry != impl_->commands.end();) {
        if (entry->first.first == object_id) {
            removals.push_back({{EventKind::capability_removed, snapshot.id, entry->first.second,
                                 snapshot.instance, snapshot.revision}, entry->second.capability.permissions, {}});
            entry = impl_->commands.erase(entry);
        } else ++entry;
    }
    impl_->live_objects.erase(snapshot.id);
    impl_->objects.erase(object);
    removals.push_back({{EventKind::object_removed, snapshot.id, {}, snapshot.instance, snapshot.revision}, {}, {}});
    impl_->publish_batch(std::move(removals));
    return Status::ok;
}

/// Update only changed state and associate its event with the new revision.
Status Runtime::update_state(std::string_view object_id, std::string state) {
    auto object = impl_->objects.find(object_id);
    if (object == impl_->objects.end()) return Status::not_found;
    if (object->second.state == state) return Status::ok;
    object->second.state = std::move(state);
    ++object->second.revision;
    const auto snapshot = object->second;
    impl_->publish({EventKind::state_changed, snapshot.id, {}, snapshot.instance, snapshot.revision});
    return Status::ok;
}

/// Bind a complete host command to an existing object without replacing a registration.
Status Runtime::add_capability(Capability capability, Validator validator, Handler handler) {
    if (capability.object_id.empty() || capability.id.empty() || !validator || !handler) return Status::invalid_argument;
    const auto object = impl_->objects.find(capability.object_id);
    if (object == impl_->objects.end()) return Status::not_found;
    const auto key = std::make_pair(capability.object_id, capability.id);
    if (impl_->commands.contains(key)) return Status::already_exists;
    const auto instance = object->second.instance;
    const auto revision = object->second.revision;
    const auto permissions = capability.permissions;
    impl_->commands.emplace(key, Impl::Entry{std::move(capability), std::move(validator),
                                            std::move(handler), impl_->next_generation++});
    impl_->publish({EventKind::capability_added, key.first, key.second, instance, revision}, permissions);
    return Status::ok;
}

/// Unregister a command and emit a permission-filtered removal event.
Status Runtime::remove_capability(std::string_view object_id, std::string_view capability_id) {
    const auto key = std::make_pair(std::string(object_id), std::string(capability_id));
    auto entry = impl_->commands.find(key);
    if (entry == impl_->commands.end()) return Status::not_found;
    const auto permissions = entry->second.capability.permissions;
    const auto snapshot = impl_->objects.at(key.first);
    impl_->commands.erase(entry);
    impl_->publish({EventKind::capability_removed, key.first, key.second, snapshot.instance, snapshot.revision}, permissions);
    return Status::ok;
}

/// Filter each object snapshot independently using the current read policy.
std::vector<Object> Runtime::objects(const AgentContext& context) const {
    std::vector<Object> result;
    for (const auto& [id, object] : impl_->objects)
        if (impl_->allow(context, Action::read_object, id)) result.push_back(object);
    return result;
}

/// Return a bounded page without copying object state or hidden object metadata.
ObjectPage Runtime::object_page(const AgentContext& context, std::string_view after, std::size_t limit) const {
    if (limit == 0 || limit > 100) return {Status::invalid_argument, {}, {}};
    ObjectPage result;
    auto current = after.empty() ? impl_->objects.begin() : impl_->objects.upper_bound(after);
    for (; current != impl_->objects.end(); ++current) {
        const auto& [id, object] = *current;
        if (!impl_->allow(context, Action::read_object, id)) continue;
        if (result.objects.size() == limit) {
            result.next_cursor = result.objects.back().id;
            break;
        }
        result.objects.push_back({object.id, object.type, object.description, object.instance, object.revision});
    }
    return result;
}

/// Hide both unknown objects and objects the supplied context cannot read.
std::optional<Object> Runtime::object(const AgentContext& context, std::string_view object_id) const {
    const auto found = impl_->objects.find(object_id);
    if (found == impl_->objects.end() || !impl_->allow(context, Action::read_object, found->first)) return std::nullopt;
    return found->second;
}

/// Apply object read and per-command discovery policies before exposing metadata.
std::vector<Capability> Runtime::capabilities(const AgentContext& context, std::string_view object_id) const {
    const auto object = impl_->objects.find(object_id);
    if (object == impl_->objects.end() || !impl_->allow(context, Action::read_object, object->first)) return {};
    std::vector<Capability> result;
    for (const auto& [key, entry] : impl_->commands) {
        if (key.first == object_id && impl_->allow(context, Action::discover_capability, key.first, key.second,
                                                  entry.capability.permissions)) result.push_back(entry.capability);
    }
    return result;
}

/// Return a bounded page without disclosing hidden commands or other objects.
CapabilityPage Runtime::capability_page(const AgentContext& context, std::string_view object_id,
                                        std::string_view after, std::size_t limit) const {
    if (limit == 0 || limit > 100) return {Status::invalid_argument, {}, {}};
    CapabilityPage result;
    const auto object = impl_->objects.find(object_id);
    if (object == impl_->objects.end() || !impl_->allow(context, Action::read_object, object->first)) return result;
    const auto start = std::make_pair(std::string(object_id), std::string(after));
    auto current = after.empty() ? impl_->commands.lower_bound(start) : impl_->commands.upper_bound(start);
    for (; current != impl_->commands.end() && current->first.first == object_id; ++current) {
        const auto& [key, entry] = *current;
        if (!impl_->allow(context, Action::discover_capability, key.first, key.second, entry.capability.permissions)) continue;
        if (result.capabilities.size() == limit) {
            result.next_cursor = result.capabilities.back().id;
            break;
        }
        result.capabilities.push_back(entry.capability);
    }
    return result;
}

/// Collect only visible objects and commands, plus individually authorized native access flags.
ProcedureAuthoringContext Runtime::procedure_authoring_context(const AgentContext& context) const {
    ProcedureAuthoringContext result;
    for (auto& snapshot : objects(context)) {
        ProcedureAuthoringObject item;
        item.object = std::move(snapshot);
        item.capabilities = capabilities(context, item.object.id);
        const auto binding = impl_->live_objects.find(item.object.id);
        if (binding != impl_->live_objects.end() && !binding->second.object.expired() &&
            impl_->allow(context, Action::read_object, item.object.id)) {
            item.native_readable = impl_->allow(context, Action::read_native_object, item.object.id);
            item.native_writable = impl_->allow(context, Action::write_native_object, item.object.id);
        }
        result.objects.push_back(std::move(item));
    }
    return result;
}

/// Reauthorize and validate before calling one host handler and publishing its outcome.
Result Runtime::invoke(const AgentContext& context, std::string_view object_id,
                       std::string_view capability_id, std::string_view input) {
    const auto key = std::make_pair(std::string(object_id), std::string(capability_id));
    const auto found = impl_->commands.find(key);
    if (found == impl_->commands.end()) return {Status::denied, "command unavailable"};
    const auto entry = found->second;
    if (!impl_->allow(context, Action::execute, key.first, key.second, entry.capability.permissions))
        return {Status::denied, "command unavailable"};
    std::string reason;
    try {
        if (!entry.validator(input, reason)) return {Status::validation_failed, reason};
    } catch (...) { return {Status::validation_failed, "validator failed"}; }
    const auto current = impl_->commands.find(key);
    if (current == impl_->commands.end() || current->second.generation != entry.generation)
        return {Status::not_found, "command registration changed"};
    if (!impl_->allow(context, Action::execute, key.first, key.second, entry.capability.permissions))
        return {Status::denied, "command unavailable"};
    const auto object = impl_->objects.at(key.first);
    Result result;
    try { result = entry.handler(context, input); }
    catch (...) { result = {Status::handler_failed, "handler failed"}; }
    const auto updated = impl_->objects.find(key.first);
    const auto revision = updated != impl_->objects.end() && updated->second.instance == object.instance
                            ? updated->second.revision : object.revision;
    impl_->publish({result.status == Status::ok ? EventKind::command_completed : EventKind::command_failed,
                    key.first, key.second, object.instance, revision}, entry.capability.permissions);
    return result;
}

/// Publish an object only after both its native binding and lifetime hook are installed.
Status Runtime::register_tracked_erased(Object descriptor, std::weak_ptr<void> object, std::type_index type,
                                        const std::shared_ptr<detail::TrackedLifetime>& lifetime,
                                        std::string& object_id) {
    if (descriptor.type.empty() || object.expired() || !lifetime) return Status::invalid_argument;
    if (descriptor.id.empty()) {
        do { descriptor.id = "application.object." + std::to_string(impl_->next_tracked_id++); }
        while (impl_->objects.contains(descriptor.id));
    }
    if (impl_->objects.contains(descriptor.id)) return Status::already_exists;
    descriptor.instance = impl_->next_instance++;
    descriptor.revision = 1;
    object_id = descriptor.id;
    const auto instance = descriptor.instance;
    const std::weak_ptr<Impl::TrackedBridge> bridge = impl_->tracked_bridge;
    lifetime->on_destroy = [bridge, id = object_id, instance] {
        const auto owner = bridge.lock();
        if (!owner) return;
        LifecycleDispatcher dispatch;
        Runtime* runtime = nullptr;
        {
            std::lock_guard lock(owner->mutex);
            if (!owner->runtime) return;
            if (owner->owner == std::this_thread::get_id()) runtime = owner->runtime;
            else {
                owner->pending.emplace_back(id, instance);
                dispatch = owner->dispatcher;
            }
        }
        if (runtime) {
            runtime->retire_tracked(id, instance);
        } else if (dispatch) {
            dispatch([bridge] {
                const auto owner = bridge.lock();
                if (!owner) return;
                Runtime* runtime = nullptr;
                {
                    std::lock_guard lock(owner->mutex);
                    if (owner->owner == std::this_thread::get_id()) runtime = owner->runtime;
                }
                if (runtime) (void)runtime->drain_lifecycle();
            });
        }
    };
    impl_->objects.emplace(object_id, std::move(descriptor));
    try { impl_->live_objects.emplace(object_id, Impl::LiveBinding{std::move(object), type}); }
    catch (...) {
        impl_->objects.erase(object_id);
        lifetime->on_destroy = {};
        throw;
    }
    impl_->publish({EventKind::object_added, object_id, {}, instance, 1});
    return Status::ok;
}

/// Set or clear the host event-loop handoff, then process work already queued.
Status Runtime::set_lifecycle_dispatcher(LifecycleDispatcher dispatcher) {
    {
        std::lock_guard lock(impl_->tracked_bridge->mutex);
        if (impl_->tracked_bridge->owner != std::this_thread::get_id()) return Status::invalid_argument;
        impl_->tracked_bridge->dispatcher = std::move(dispatcher);
    }
    return drain_lifecycle();
}

/// Remove objects whose last shared owner was released outside the runtime thread.
Status Runtime::drain_lifecycle() {
    std::deque<std::pair<std::string, std::uint64_t>> pending;
    {
        std::lock_guard lock(impl_->tracked_bridge->mutex);
        if (impl_->tracked_bridge->owner != std::this_thread::get_id()) return Status::invalid_argument;
        pending.swap(impl_->tracked_bridge->pending);
    }
    for (const auto& [id, instance] : pending) retire_tracked(id, instance);
    return Status::ok;
}

/// Retire only the original instance, preserving any replacement that reused its identifier.
void Runtime::retire_tracked(std::string_view object_id, std::uint64_t instance) {
    const auto found = impl_->objects.find(object_id);
    if (found != impl_->objects.end() && found->second.instance == instance) (void)remove_object(object_id);
}

/// Register a weak native object binding without taking application ownership.
Status Runtime::bind_live_erased(std::string_view object_id, std::weak_ptr<void> object, std::type_index type) {
    if (object.expired()) return Status::invalid_argument;
    const auto found = impl_->objects.find(object_id);
    if (found == impl_->objects.end()) return Status::not_found;
    if (impl_->live_objects.contains(found->first)) return Status::already_exists;
    impl_->live_objects.emplace(found->first, Impl::LiveBinding{std::move(object), type});
    return Status::ok;
}

/// Remove a native object binding while leaving the registry entry untouched.
Status Runtime::unbind_live_object(std::string_view object_id) {
    const auto found = impl_->live_objects.find(object_id);
    if (found == impl_->live_objects.end()) return Status::not_found;
    impl_->live_objects.erase(found);
    return Status::ok;
}

/// Check the caller and exact type before locking the host's weak reference.
std::shared_ptr<void> Runtime::acquire_live_erased(const AgentContext& context, std::string_view object_id,
                                                   std::type_index type, Action action) const {
    const auto found = impl_->live_objects.find(object_id);
    if (found == impl_->live_objects.end() || found->second.type != type) return {};
    const auto id = std::string(object_id);
    if (!impl_->allow(context, Action::read_object, id) || !impl_->allow(context, action, id)) return {};
    return found->second.object.lock();
}

/// Authorize a trusted procedure and execute it exactly once with a scoped application view.
Result Runtime::execute_procedure(const AgentContext& context, std::string_view procedure_id,
                                  NativeProcedure procedure, std::string_view input) {
    if (procedure_id.empty() || !procedure) return {Status::invalid_argument, "procedure is required"};
    const auto id = std::string(procedure_id);
    if (!impl_->allow(context, Action::execute_procedure, "application.procedure", id))
        return {Status::denied, "procedure unavailable"};
    ProcedureContext scoped(*this, context);
    Result result;
    try { result = procedure(scoped, input); }
    catch (...) { result = {Status::handler_failed, "procedure failed"}; }
    impl_->publish({result.status == Status::ok ? EventKind::procedure_completed : EventKind::procedure_failed,
                    "application.procedure", id, 0, 0});
    return result;
}

/// Reject incompatible or incomplete module descriptors before entering native procedure code.
Result Runtime::execute_procedure(const AgentContext& context, const NativeProcedureDescriptor& descriptor,
                                  std::string_view input) {
    if (descriptor.size < sizeof(NativeProcedureDescriptor) ||
        descriptor.version != native_procedure_descriptor_version ||
        !descriptor.identifier || !*descriptor.identifier || !descriptor.entry)
        return {Status::invalid_argument, "invalid procedure descriptor"};
    return execute_procedure(context, descriptor.identifier, descriptor.entry, input);
}

/// Assemble a module around the agent's procedure body without interpreting its C++ code.
Result compose_procedure_source(const ProcedureDraft& draft) {
    if (draft.identifier.empty() || draft.body.empty() || draft.identifier.contains('\0') ||
        draft.preamble.contains('\0') || draft.body.contains('\0'))
        return {Status::invalid_argument, "identifier and body are required and cannot contain NUL"};
    // Fixed-width octal escapes prevent a following digit or quote from changing the literal.
    std::string quoted = "\"";
    for (unsigned char byte : draft.identifier) {
        quoted += '\\';
        quoted += static_cast<char>('0' + ((byte >> 6) & 7));
        quoted += static_cast<char>('0' + ((byte >> 3) & 7));
        quoted += static_cast<char>('0' + (byte & 7));
    }
    quoted += '"';
    std::string source = "#include <iiAgentApplicationInterface>\n";
    source += draft.preamble;
    source += "\nnamespace {\n"
              "iiAgentApplicationInterface::Result iiagent_application_authored_body(\n"
              "    iiAgentApplicationInterface::ProcedureContext& context, std::string_view input) {\n";
    source += draft.body;
    source += "\n}\n}\n"
              "#if defined(_WIN32)\n"
              "#define IIAGENT_AUTHORED_EXPORT __declspec(dllexport)\n"
              "#elif defined(__GNUC__) || defined(__clang__)\n"
              "#define IIAGENT_AUTHORED_EXPORT __attribute__((visibility(\"default\")))\n"
              "#else\n#define IIAGENT_AUTHORED_EXPORT\n#endif\n"
              "extern \"C\" IIAGENT_AUTHORED_EXPORT const iiAgentApplicationInterface::NativeProcedureDescriptor*\n"
              "iiagent_application_procedure_v1() noexcept {\n"
              "    static const iiAgentApplicationInterface::NativeProcedureDescriptor descriptor{\n"
              "        sizeof(iiAgentApplicationInterface::NativeProcedureDescriptor),\n"
              "        iiAgentApplicationInterface::native_procedure_descriptor_version, ";
    source += quoted;
    source += ", &iiagent_application_authored_body};\n    return &descriptor;\n}\n#undef IIAGENT_AUTHORED_EXPORT\n";
    return {Status::ok, std::move(source)};
}

/// Compile one artifact after checking the requesting principal's compilation policy.
ProcedureBuild Runtime::build_source(const AgentContext& context, std::string_view procedure_id,
                                     std::string_view source, SourceProcedureBackend& backend) {
    if (procedure_id.empty() || source.empty() || procedure_id.contains('\0') || source.contains('\0'))
        return {Status::invalid_argument, "source and procedure are required and cannot contain NUL", {}};
    const auto id = std::string(procedure_id);
    if (!impl_->allow(context, Action::compile_procedure, "application.procedure", id))
        return {Status::denied, "procedure compilation unavailable", {}};
    try {
        auto compiled = backend.compile(id, source);
        if (compiled.status != Status::ok) return {compiled.status, std::move(compiled.diagnostic), {}};
        if (!compiled.artifact) return {Status::handler_failed, "compiler returned no artifact", {}};
        auto procedure = std::unique_ptr<CompiledProcedure>(
            new CompiledProcedure(id, backend, std::move(compiled.artifact)));
        return {Status::ok, std::move(compiled.diagnostic), std::move(procedure)};
    } catch (...) { return {Status::handler_failed, "source procedure backend failed", {}}; }
}

/// Assemble an agent-authored procedure and compile it without executing it.
ProcedureBuild Runtime::build_procedure(const AgentContext& context, const ProcedureDraft& draft,
                                        SourceProcedureBackend& backend) {
    const auto source = compose_procedure_source(draft);
    if (source.status != Status::ok) return {source.status, source.value, {}};
    return build_source(context, draft.identifier, source.value, backend);
}

/// Reauthorize and load an existing artifact before each independent execution.
Result Runtime::execute_built(const AgentContext& context, const CompiledProcedure& procedure,
                              SourceProcedureBackend& backend, std::string_view input) {
    if (procedure.backend_ != &backend || !procedure.artifact_)
        return {Status::invalid_argument, "procedure belongs to a different backend"};
    if (!impl_->allow(context, Action::load_procedure, "application.procedure", procedure.identifier_))
        return {Status::denied, "procedure loading unavailable"};
    try {
        auto loaded = backend.load(*procedure.artifact_);
        if (loaded.status != Status::ok) return {loaded.status, std::move(loaded.diagnostic)};
        if (!loaded.module) return {Status::handler_failed, "loader returned no module"};
        const auto* descriptor = loaded.module->descriptor();
        if (!descriptor || descriptor->size < sizeof(NativeProcedureDescriptor) ||
            descriptor->version != native_procedure_descriptor_version || !descriptor->identifier ||
            procedure.identifier_ != descriptor->identifier || !descriptor->entry)
            return {Status::invalid_argument, "invalid or mismatched procedure descriptor"};
        return execute_procedure(context, *descriptor, input);
    } catch (...) { return {Status::handler_failed, "source procedure backend failed"}; }
}

/// Independently authorize source compilation, module loading and procedure execution.
Result Runtime::execute_source(const AgentContext& context, std::string_view procedure_id,
                               std::string_view source, SourceProcedureBackend& backend, std::string_view input) {
    auto built = build_source(context, procedure_id, source, backend);
    if (built.status != Status::ok) return {built.status, std::move(built.diagnostic)};
    return execute_built(context, *built.procedure, backend, input);
}

/// Query snapshots using this procedure's identity and the runtime's current policy.
std::vector<Object> ProcedureContext::objects() const { return runtime_->objects(context_); }
/// Read a snapshot using the procedure's authenticated identity.
std::optional<Object> ProcedureContext::object(std::string_view object_id) const {
    return runtime_->object(context_, object_id);
}
/// Discover the commands allowed to the procedure's identity.
std::vector<Capability> ProcedureContext::capabilities(std::string_view object_id) const {
    return runtime_->capabilities(context_, object_id);
}
/// Invoke the application's existing validation and command execution path.
Result ProcedureContext::invoke(std::string_view object_id, std::string_view capability_id,
                                std::string_view input) const {
    return runtime_->invoke(context_, object_id, capability_id, input);
}

/// Attach one observer to events published after registration for the runtime lifetime.
Subscription Runtime::subscribe(AgentContext context, Observer observer) {
    if (!observer) return 0;
    const auto id = impl_->next_subscription++;
    impl_->observers.emplace(id, Impl::Subscriber{std::move(context), std::move(observer)});
    return id;
}

/// Prevent later queued or newly published events from reaching this observer.
void Runtime::unsubscribe(Subscription subscription) { impl_->observers.erase(subscription); }

} // namespace iiAgentApplicationInterface
