#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace iiAgentApplicationInterface {

/// Stable result categories for the first in-process API. They do not imply rollback.
enum class Status : int {
    ok = 0, invalid_argument, not_found, already_exists, denied,
    validation_failed, handler_failed, internal_error
};

/// Result of a command: value is the output on success and a host diagnostic on failure.
struct Result {
    Status status = Status::ok;
    std::string value;
};

/// Host-authenticated identity; the runtime does not authenticate this string.
struct AgentContext {
    std::string principal;
};

/// Operations are authorized independently, including every event delivery.
enum class Action : int {
    read_object, discover_capability, execute, observe,
    read_native_object, write_native_object, execute_procedure, compile_procedure, load_procedure
};

/// Information given to the host policy; permissions are the command's declared names.
struct AccessRequest {
    AgentContext context;
    Action action;
    std::string object_id;
    std::string capability_id;
    std::vector<std::string> permissions;
};

/// Immutable snapshot of a registered object and its host-published state.
struct Object {
    std::string id;
    std::string type;
    std::string description;
    std::string state;
    std::uint64_t instance = 0;
    std::uint64_t revision = 0;
};

/// A discoverable object identity and revision without its potentially large state.
struct ObjectHeader {
    std::string id;
    std::string type;
    std::string description;
    std::uint64_t instance = 0;
    std::uint64_t revision = 0;
};

/// A bounded, authorization-filtered page; next_cursor is empty on the final page.
struct ObjectPage {
    Status status = Status::ok;
    std::vector<ObjectHeader> objects;
    std::string next_cursor;
};

/// Application-owned semantic command and opaque schemas for discovery.
struct Capability {
    std::string object_id;
    std::string id;
    std::string description;
    std::string input_schema;
    std::string output_schema;
    std::vector<std::string> permissions;
};

/// A bounded page of discoverable commands for one authorized application object.
struct CapabilityPage {
    Status status = Status::ok;
    std::vector<Capability> capabilities;
    std::string next_cursor;
};

/// Changes published synchronously to currently authorized subscribers.
enum class EventKind : int {
    object_added, object_removed, capability_added, capability_removed,
    state_changed, command_completed, command_failed, procedure_completed, procedure_failed
};

/// Change identity without the potentially sensitive state or command result.
struct Event {
    EventKind kind;
    std::string object_id;
    std::string capability_id;
    std::uint64_t instance;
    std::uint64_t revision;
};

/// Host callback for an access decision. Exceptions are interpreted as denial.
using Authorizer = std::function<bool(const AccessRequest&)>;
/// Host validator for one command payload; reason is returned on rejection.
using Validator = std::function<bool(std::string_view, std::string& reason)>;
/// Host command. It can publish state and is called at most once per invocation.
using Handler = std::function<Result(const AgentContext&, std::string_view)>;
/// Host callback for a change notification. Exceptions are isolated.
using Observer = std::function<void(const Event&)>;
/// A nonzero token identifies a subscription for the lifetime of one runtime.
using Subscription = std::uint64_t;
/// Thread-safe host callback that posts work to the runtime's owner thread.
using LifecycleDispatcher = std::function<void(std::function<void()>)>;

class ProcedureContext;
class SourceProcedureBackend;
class CompiledProcedure;
struct ProcedureBuild;
/// Trusted host-supplied native code, executed synchronously in the application process.
using NativeProcedure = std::function<Result(ProcedureContext&, std::string_view)>;
/// Entry function compiled with the same C++ ABI as the host application.
using NativeProcedureEntry = Result (*)(ProcedureContext&, std::string_view);
/// Version of the descriptor layout; this does not guarantee a stable C++ ABI.
inline constexpr std::uint32_t native_procedure_descriptor_version = 1;
/// Export name a host loader may use to locate a native procedure descriptor.
inline constexpr std::string_view native_procedure_entry_symbol = "iiagent_application_procedure_v1";
/// Metadata returned by a loaded procedure module; the host keeps its module alive while using it.
struct NativeProcedureDescriptor {
    /// Byte size of this descriptor, allowing the host to reject incompatible layouts.
    std::uint32_t size;
    /// Descriptor layout version; must equal native_procedure_descriptor_version.
    std::uint32_t version;
    /// Stable, nonempty identifier supplied to the host's procedure authorization policy.
    const char* identifier;
    /// Procedure entry compiled for the host application's C++ ABI.
    NativeProcedureEntry entry;
};
/// Signature of the exported C-linkage descriptor provider in a native procedure module.
using NativeProcedureDescriptorEntry = const NativeProcedureDescriptor* (*)() noexcept;

/// One readable application object and its discoverable, authorized authoring metadata.
struct ProcedureAuthoringObject {
    Object object;
    std::vector<Capability> capabilities;
    bool native_readable = false;
    bool native_writable = false;
};

/// A discoverable C++ API declaration and concise behavioral contract.
struct ProcedureApiItem {
    std::string_view name;
    std::string_view signature;
    std::string_view description;
};

/// ProcedureContext operations available inside every generated procedure body.
inline constexpr ProcedureApiItem procedure_context_api[] = {
    {"subject", "const AgentContext& context.subject() const", "Return the host-authenticated principal."},
    {"objects", "std::vector<Object> context.objects() const", "List object snapshots currently authorized for reading."},
    {"object", "std::optional<Object> context.object(std::string_view object_id) const", "Return one readable snapshot or no value."},
    {"capabilities", "std::vector<Capability> context.capabilities(std::string_view object_id) const", "List currently discoverable commands for a readable object."},
    {"invoke", "Result context.invoke(std::string_view object_id, std::string_view capability_id, std::string_view input) const", "Authorize, validate and invoke the application's command; check Result.status."},
    {"read_object<T>", "std::shared_ptr<const T> context.read_object<T>(std::string_view object_id) const", "Pin an exact-type live object after separate native-read authorization; otherwise return empty."},
    {"write_object<T>", "std::shared_ptr<T> context.write_object<T>(std::string_view object_id) const", "Pin an exact-type live object after separate native-write authorization; otherwise return empty."}
};

/// Data types agents can use with the generated C++ procedure entry.
inline constexpr ProcedureApiItem procedure_type_api[] = {
    {"Result", "struct Result { Status status; std::string value; }", "On success value is output; on failure it is a diagnostic. Failure does not imply rollback."},
    {"Status", "enum class Status { ok, invalid_argument, not_found, already_exists, denied, validation_failed, handler_failed, internal_error }", "Compare command results with iiAgentApplicationInterface::Status::ok."},
    {"Object", "struct Object { std::string id, type, description, state; std::uint64_t instance, revision; }", "An authorized application snapshot; type is host metadata, not necessarily a C++ type name."},
    {"Capability", "struct Capability { std::string object_id, id, description, input_schema, output_schema; std::vector<std::string> permissions; }", "A discoverable command; schemas are opaque host-provided descriptions."},
    {"AgentContext", "struct AgentContext { std::string principal; }", "Identity supplied and authenticated by the application host."}
};

/// Point-in-time application interface and native procedure contract for an agent.
struct ProcedureAuthoringContext {
    std::string_view language = "C++23";
    std::string_view entry_symbol = native_procedure_entry_symbol;
    std::string_view body_signature =
        "iiAgentApplicationInterface::Result(iiAgentApplicationInterface::ProcedureContext& context, std::string_view input)";
    std::span<const ProcedureApiItem> context_api = procedure_context_api;
    std::span<const ProcedureApiItem> type_api = procedure_type_api;
    std::vector<ProcedureAuthoringObject> objects;
};

/// Agent-authored procedure components. The host must trust both source fields.
struct ProcedureDraft {
    std::string identifier;
    std::string preamble;
    std::string body;
};

/// Assemble a complete procedure module, escaping its identifier as a C++ string literal.
/// Empty identifiers/bodies and embedded NUL characters return invalid_argument.
[[nodiscard]] Result compose_procedure_source(const ProcedureDraft& draft);

namespace detail {
/// One destruction callback, armed only after an object has been registered successfully.
struct TrackedLifetime {
    std::function<void()> on_destroy;
    /// Deliver at most once; destruction cannot propagate an exception.
    void release() noexcept {
        try {
            auto callback = std::move(on_destroy);
            if (callback) callback();
        } catch (...) { /* Destruction must remain noexcept. */ }
    }
};
}

/// Result and generated identity of an automatically tracked host-owned object.
template<class T> struct TrackedObjectResult {
    Status status;
    std::string object_id;
    std::shared_ptr<T> object;
};

/// Single-threaded, in-process registry and command dispatcher owned by a trusted host.
class Runtime final {
public:
    /// Creates a runtime; an absent authorizer denies all consumer access.
    explicit Runtime(Authorizer authorizer = {});
    /// Releases the registry, handlers and all subscriptions.
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    /// Whether the caller is on the thread that created and serializes this runtime.
    [[nodiscard]] bool is_owner_thread() const noexcept;
    /// Registers a host-owned object, assigning a new instance and initial revision.
    Status add_object(Object object);
    /// Removes the object and its commands; unknown objects return not_found.
    Status remove_object(std::string_view object_id);
    /// Publishes host state and emits a change only when the value differs.
    Status update_state(std::string_view object_id, std::string state);
    /// Registers a command with mandatory validator and handler on an existing object.
    Status add_capability(Capability capability, Validator validator, Handler handler);
    /// Removes one command without altering its object.
    Status remove_capability(std::string_view object_id, std::string_view capability_id);

    /// Returns only objects the policy permits this identity to read.
    [[nodiscard]] std::vector<Object> objects(const AgentContext& context) const;
    /// Returns at most 100 authorized identities after a lexicographic ID cursor.
    /// Object state is never copied into this metadata-only page.
    [[nodiscard]] ObjectPage object_page(const AgentContext& context, std::string_view after,
                                         std::size_t limit) const;
    /// Returns an authorized object snapshot or an empty result for hidden/unknown objects.
    [[nodiscard]] std::optional<Object> object(const AgentContext& context, std::string_view object_id) const;
    /// Returns discoverable commands only when the associated object is also readable.
    [[nodiscard]] std::vector<Capability> capabilities(const AgentContext& context, std::string_view object_id) const;
    /// Returns at most 100 discoverable commands after a lexicographic command ID.
    [[nodiscard]] CapabilityPage capability_page(const AgentContext& context, std::string_view object_id,
                                                 std::string_view after, std::size_t limit) const;
    /// Describes the procedure ABI and current objects/commands visible to this principal.
    /// Native access flags reflect policy and weak binding lifetime only at this instant.
    [[nodiscard]] ProcedureAuthoringContext procedure_authoring_context(const AgentContext& context) const;
    /// Reauthorizes, validates and calls the registered host command at most once.
    [[nodiscard]] Result invoke(const AgentContext& context, std::string_view object_id,
                                std::string_view capability_id, std::string_view input);
    /// Associates a host-owned, shared-lifetime native object with an existing object ID.
    /// The registration is weak: the application retains ownership and exact static type.
    template<class T>
    Status bind_live_object(std::string_view object_id, const std::shared_ptr<T>& object) {
        static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>);
        if (!object) return Status::invalid_argument;
        return bind_live_erased(object_id, std::weak_ptr<void>(object), std::type_index(typeid(T)));
    }
    /// Constructs a shared object and automatically registers its creation and destruction.
    /// An empty ID is assigned by the runtime. A final reference released elsewhere is
    /// retired on the owner thread through the configured lifecycle dispatcher or drain.
    /// Construction exceptions propagate.
    template<class T, class... Args>
    [[nodiscard]] TrackedObjectResult<T> make_tracked_object(Object descriptor, Args&&... args) {
        static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>);
        auto lifetime = std::make_shared<detail::TrackedLifetime>();
        auto object = std::shared_ptr<T>(new T(std::forward<Args>(args)...),
            [lifetime](T* value) noexcept { delete value; lifetime->release(); });
        if (descriptor.type.empty()) descriptor.type = typeid(T).name();
        std::string object_id;
        const auto status = register_tracked_erased(std::move(descriptor), std::weak_ptr<void>(object),
                                                    std::type_index(typeid(T)), lifetime, object_id);
        if (status != Status::ok) object.reset();
        return {status, std::move(object_id), std::move(object)};
    }
    /// Sets the host's thread-safe owner-thread dispatcher and drains pending retirements.
    /// The dispatcher must outlive any calls to it and schedule work on the owner thread.
    /// An empty dispatcher leaves off-thread retirements queued for explicit draining.
    Status set_lifecycle_dispatcher(LifecycleDispatcher dispatcher);
    /// Retires objects queued by other threads; only the owner thread may call this.
    Status drain_lifecycle();
    /// Removes a native binding without removing its object metadata.
    Status unbind_live_object(std::string_view object_id);
    /// Runs trusted native procedure code after the host authorizes its subject and identifier.
    [[nodiscard]] Result execute_procedure(const AgentContext& context, std::string_view procedure_id,
                                           NativeProcedure procedure, std::string_view input);
    /// Validates a loaded procedure descriptor and runs its entry under host authorization.
    [[nodiscard]] Result execute_procedure(const AgentContext& context,
                                           const NativeProcedureDescriptor& descriptor, std::string_view input);
    /// Authorizes, compiles, loads and runs trusted source via the application's backend.
    /// Artifact and module remain alive throughout the synchronous procedure call.
    [[nodiscard]] Result execute_source(const AgentContext& context, std::string_view procedure_id,
                                        std::string_view source, SourceProcedureBackend& backend,
                                        std::string_view input);
    /// Authorizes and compiles a complete source module without loading or running it.
    /// The backend must outlive the returned compiled procedure.
    [[nodiscard]] ProcedureBuild build_source(const AgentContext& context, std::string_view procedure_id,
                                              std::string_view source, SourceProcedureBackend& backend);
    /// Assembles an agent draft and compiles it without loading or running it.
    [[nodiscard]] ProcedureBuild build_procedure(const AgentContext& context, const ProcedureDraft& draft,
                                                 SourceProcedureBackend& backend);
    /// Reauthorizes loading and execution on each use of an artifact from this backend.
    [[nodiscard]] Result execute_built(const AgentContext& context, const CompiledProcedure& procedure,
                                       SourceProcedureBackend& backend, std::string_view input);
    /// Subscribes to individually authorized events; returns zero for an empty callback.
    Subscription subscribe(AgentContext context, Observer observer);
    /// Idempotently disconnects one subscription before subsequent delivery.
    void unsubscribe(Subscription subscription);

private:
    friend class ProcedureContext;
    /// Atomically registers the snapshot, weak native binding and destruction callback.
    Status register_tracked_erased(Object descriptor, std::weak_ptr<void> object, std::type_index type,
                                   const std::shared_ptr<detail::TrackedLifetime>& lifetime, std::string& object_id);
    /// Removes a tracked registration only if the identifier still denotes the same instance.
    void retire_tracked(std::string_view object_id, std::uint64_t instance);
    /// Erases the host's exact static type while preserving weak ownership.
    Status bind_live_erased(std::string_view object_id, std::weak_ptr<void> object, std::type_index type);
    /// Rechecks policy and exact type, then pins a live object for the caller's use.
    [[nodiscard]] std::shared_ptr<void> acquire_live_erased(const AgentContext& context,
        std::string_view object_id, std::type_index type, Action action) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Scoped application access for a trusted native procedure and its authenticated subject.
/// This object exists only during the procedure call; it is not a sandbox.
class ProcedureContext final {
public:
    ProcedureContext(const ProcedureContext&) = delete;
    ProcedureContext& operator=(const ProcedureContext&) = delete;
    /// Returns the host-provided identity associated with this procedure.
    [[nodiscard]] const AgentContext& subject() const noexcept { return context_; }
    /// Lists currently readable application objects using the runtime's policy.
    [[nodiscard]] std::vector<Object> objects() const;
    /// Reads one authorized object snapshot.
    [[nodiscard]] std::optional<Object> object(std::string_view object_id) const;
    /// Discovers authorized commands for one readable object.
    [[nodiscard]] std::vector<Capability> capabilities(std::string_view object_id) const;
    /// Executes the application's existing command path with the same identity.
    [[nodiscard]] Result invoke(std::string_view object_id, std::string_view capability_id,
                                std::string_view input) const;
    /// Pins a live object as read-only after native-read and object-read authorization.
    template<class T>
    [[nodiscard]] std::shared_ptr<const T> read_object(std::string_view object_id) const {
        static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>);
        return std::static_pointer_cast<const T>(runtime_->acquire_live_erased(
            context_, object_id, std::type_index(typeid(T)), Action::read_native_object));
    }
    /// Pins a mutable live object after native-write and object-read authorization.
    template<class T>
    [[nodiscard]] std::shared_ptr<T> write_object(std::string_view object_id) const {
        static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>);
        return std::static_pointer_cast<T>(runtime_->acquire_live_erased(
            context_, object_id, std::type_index(typeid(T)), Action::write_native_object));
    }

private:
    friend class Runtime;
    /// Binds this restricted view to the lifetime of one synchronous call.
    ProcedureContext(Runtime& runtime, AgentContext context) : runtime_(&runtime), context_(std::move(context)) {}
    Runtime* runtime_;
    AgentContext context_;
};

} // namespace iiAgentApplicationInterface
