#include "AgentC.h"
#include "Agent.h"
#include "SourceProcedure.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace iiAgentApplicationInterface;

static_assert(static_cast<int>(Status::internal_error) == IIAGENTAPPLICATIONINTERFACE_INTERNAL_ERROR);
static_assert(static_cast<int>(Action::execute) == IIAGENTAPPLICATIONINTERFACE_EXECUTE);
static_assert(static_cast<int>(Action::load_procedure) == IIAGENTAPPLICATIONINTERFACE_LOAD_PROCEDURE);
static_assert(static_cast<int>(EventKind::procedure_failed) == IIAGENTAPPLICATIONINTERFACE_PROCEDURE_FAILED);

/// C-owned wrapper keeps the C++ runtime and its authorization callback together.
struct iiagent_application_interface_runtime {
    explicit iiagent_application_interface_runtime(Authorizer policy) : native(std::move(policy)) {}
    Runtime native;
};

namespace {
/// Convert a native status to the integer shared by the C and Python interfaces.
int code(Status status) { return static_cast<int>(status); }

/// Catch all native exceptions before they cross the C ABI boundary.
template<class F> int guarded(F&& function) noexcept {
    try { return function(); } catch (...) { return code(Status::internal_error); }
}

/// Encode one UTF-8 text value as a JSON string without interpreting it as JSON.
std::string quoted(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    out += '"';
    return out;
}

/// Serialize an object snapshot while keeping the state an opaque string.
std::string object_json(const Object& object) {
    return "{\"id\":" + quoted(object.id) + ",\"type\":" + quoted(object.type) +
           ",\"description\":" + quoted(object.description) + ",\"state\":" + quoted(object.state) +
           ",\"instance\":" + std::to_string(object.instance) + ",\"revision\":" + std::to_string(object.revision) + "}";
}

/// Serialize discovered metadata without treating schema strings as trusted JSON.
std::string capability_json(const Capability& capability) {
    std::string out = "{\"object_id\":" + quoted(capability.object_id) + ",\"id\":" + quoted(capability.id) +
        ",\"description\":" + quoted(capability.description) + ",\"input_schema\":" + quoted(capability.input_schema) +
        ",\"output_schema\":" + quoted(capability.output_schema) + ",\"permissions\":[";
    bool first = true;
    for (const auto& permission : capability.permissions) {
        if (!first) out += ',';
        first = false;
        out += quoted(permission);
    }
    return out + "]}";
}

/// Serialize an agent's filtered authoring context without treating schemas as JSON.
std::string authoring_json(const ProcedureAuthoringContext& context) {
    std::string out = "{\"format\":\"iiAgentApplicationInterface.procedure-authoring.v1\",\"language\":" +
        quoted(std::string(context.language)) + ",\"header\":\"<iiAgentApplicationInterface>\",\"entry_symbol\":" +
        quoted(std::string(context.entry_symbol)) + ",\"descriptor_version\":" +
        std::to_string(native_procedure_descriptor_version) + ",\"body_signature\":" +
        quoted(std::string(context.body_signature)) + ",\"context_methods\":[";
    bool first_method = true;
    for (const auto& method : context.context_api) {
        if (!first_method) out += ',';
        first_method = false;
        out += quoted(std::string(method.name));
    }
    const auto append_api = [&](std::string_view name, std::span<const ProcedureApiItem> items) {
        out += "],\"" + std::string(name) + "\":[";
        bool first = true;
        for (const auto& item : items) {
            if (!first) out += ',';
            first = false;
            out += "{\"name\":" + quoted(std::string(item.name)) +
                ",\"signature\":" + quoted(std::string(item.signature)) +
                ",\"description\":" + quoted(std::string(item.description)) + "}";
        }
    };
    append_api("context_api", context.context_api);
    append_api("type_api", context.type_api);
    out += "],\"objects\":[";
    bool first_object = true;
    for (const auto& item : context.objects) {
        if (!first_object) out += ',';
        first_object = false;
        out += "{\"object\":" + object_json(item.object) + ",\"capabilities\":[";
        bool first_capability = true;
        for (const auto& capability : item.capabilities) {
            if (!first_capability) out += ',';
            first_capability = false;
            out += capability_json(capability);
        }
        out += "],\"native_readable\":";
        out += item.native_readable ? "true" : "false";
        out += ",\"native_writable\":";
        out += item.native_writable ? "true}" : "false}";
    }
    return out + "]}";
}

/// Return a malloc-owned C string that the caller releases with iiagent_application_interface_free.
int output_string(const std::string& value, char **out) {
    if (!out) return code(Status::invalid_argument);
    *out = static_cast<char*>(std::malloc(value.size() + 1));
    if (!*out) return code(Status::internal_error);
    std::memcpy(*out, value.c_str(), value.size() + 1);
    return code(Status::ok);
}
/// Owns a host callback's compilation artifact until the runtime completes the call.
struct CallbackArtifact final : SourceArtifact {
    CallbackArtifact(iiagent_application_interface_source_backend callbacks, std::uint64_t handle)
        : callbacks_(callbacks), handle_(handle) {}
    ~CallbackArtifact() override { try { callbacks_.release_artifact(callbacks_.user, handle_); } catch (...) {} }
    iiagent_application_interface_source_backend callbacks_;
    std::uint64_t handle_;
};

/// Owns a host callback's loaded module and exposes its borrowed native descriptor.
struct CallbackModule final : LoadedSourceProcedure {
    CallbackModule(iiagent_application_interface_source_backend callbacks, std::uint64_t handle,
                   const NativeProcedureDescriptor* descriptor)
        : callbacks_(callbacks), handle_(handle), descriptor_(descriptor) {}
    ~CallbackModule() override { try { callbacks_.release_module(callbacks_.user, handle_); } catch (...) {} }
    const NativeProcedureDescriptor* descriptor() const noexcept override { return descriptor_; }
    iiagent_application_interface_source_backend callbacks_;
    std::uint64_t handle_;
    const NativeProcedureDescriptor* descriptor_;
};

/// Adapt the C host callbacks to the runtime's replaceable compiler and loader port.
class CallbackSourceBackend final : public SourceProcedureBackend {
public:
    explicit CallbackSourceBackend(iiagent_application_interface_source_backend callbacks) : callbacks_(callbacks) {}
    SourceCompilation compile(std::string_view identifier, std::string_view source) override {
        const std::string name(identifier), text(source);
        std::uint64_t handle = 0;
        const char* diagnostic = nullptr;
        const int status = callbacks_.compile(callbacks_.user, name.c_str(), text.c_str(), &handle, &diagnostic);
        /// Release even if copying the diagnostic or constructing the owning wrapper fails.
        struct Guard {
            iiagent_application_interface_source_backend callbacks;
            std::uint64_t handle;
            ~Guard() { if (handle) try { callbacks.release_artifact(callbacks.user, handle); } catch (...) {} }
        } guard{callbacks_, handle};
        const std::string message = diagnostic ? diagnostic : "";
        if (status != 0 || !handle) return {Status::handler_failed, message, {}};
        auto owned = std::make_unique<CallbackArtifact>(callbacks_, handle);
        guard.handle = 0;
        return {Status::ok, message, std::move(owned)};
    }
    SourceLoad load(const SourceArtifact& artifact) override {
        const auto* input = dynamic_cast<const CallbackArtifact*>(&artifact);
        if (!input) return {Status::invalid_argument, "invalid artifact", {}};
        std::uint64_t handle = 0;
        const void* descriptor = nullptr;
        const char* diagnostic = nullptr;
        const int status = callbacks_.load(callbacks_.user, input->handle_, &handle, &descriptor, &diagnostic);
        /// Pair every callback-issued module handle with release on all exit paths.
        struct Guard {
            iiagent_application_interface_source_backend callbacks;
            std::uint64_t handle;
            ~Guard() { if (handle) try { callbacks.release_module(callbacks.user, handle); } catch (...) {} }
        } guard{callbacks_, handle};
        const std::string message = diagnostic ? diagnostic : "";
        if (status != 0 || !handle || !descriptor) return {Status::handler_failed, message, {}};
        auto owned = std::make_unique<CallbackModule>(callbacks_, handle,
                    static_cast<const NativeProcedureDescriptor*>(descriptor));
        guard.handle = 0;
        return {Status::ok, message, std::move(owned)};
    }
private:
    iiagent_application_interface_source_backend callbacks_;
};

/// Validate a complete, versioned callback table before any host callback is invoked.
bool valid_backend(const iiagent_application_interface_source_backend* callbacks) {
    return callbacks && callbacks->size >= sizeof(*callbacks) && callbacks->compile && callbacks->load &&
        callbacks->release_artifact && callbacks->release_module;
}
}

/// Keeps callback storage and the artifact in the right destruction order.
struct iiagent_application_interface_compiled_procedure {
    explicit iiagent_application_interface_compiled_procedure(iiagent_application_interface_source_backend callbacks)
        : backend(callbacks) {}
    CallbackSourceBackend backend;
    std::unique_ptr<CompiledProcedure> procedure;
};

namespace {
/// Compile into a stable backend wrapper and return ownership only on complete success.
template<class Build> int create_compiled(const iiagent_application_interface_source_backend* callbacks,
    iiagent_application_interface_compiled_procedure** out_procedure, char** out_text, Build&& build) {
    if (!valid_backend(callbacks) || !out_procedure || !out_text) return code(Status::invalid_argument);
    auto holder = std::make_unique<iiagent_application_interface_compiled_procedure>(*callbacks);
    auto result = build(holder->backend);
    const auto written = output_string(result.diagnostic, out_text);
    if (written != code(Status::ok)) return written;
    if (result.status != Status::ok) return code(result.status);
    if (!result.procedure) return code(Status::internal_error);
    holder->procedure = std::move(result.procedure);
    *out_procedure = holder.release();
    return code(Status::ok);
}
}

extern "C" {

iiagent_application_interface_runtime *iiagent_application_interface_create(iiagent_application_interface_authorizer policy, void *user) {
    try {
        Authorizer authorize;
        if (policy) authorize = [policy, user](const AccessRequest& request) {
            std::vector<const char*> permissions;
            permissions.reserve(request.permissions.size());
            for (const auto& item : request.permissions) permissions.push_back(item.c_str());
            return policy(user, request.context.principal.c_str(), static_cast<int>(request.action),
                          request.object_id.c_str(), request.capability_id.c_str(), permissions.data(), permissions.size()) != 0;
        };
        return new iiagent_application_interface_runtime(std::move(authorize));
    } catch (...) { return nullptr; }
}

void iiagent_application_interface_destroy(iiagent_application_interface_runtime *runtime) { delete runtime; }
void iiagent_application_interface_free(void *memory) { std::free(memory); }

int iiagent_application_interface_add_object(
    iiagent_application_interface_runtime *r, const char *id, const char *type,
    const char *description, const char *state) {
    return guarded([&] { if (!r || !id || !type || !description || !state) return code(Status::invalid_argument);
        return code(r->native.add_object({id, type, description, state})); });
}
int iiagent_application_interface_remove_object(iiagent_application_interface_runtime *r, const char *id) {
    return guarded([&] { return !r || !id ? code(Status::invalid_argument) : code(r->native.remove_object(id)); });
}
int iiagent_application_interface_update_state(iiagent_application_interface_runtime *r, const char *id, const char *state) {
    return guarded([&] { return !r || !id || !state ? code(Status::invalid_argument) : code(r->native.update_state(id, state)); });
}
int iiagent_application_interface_add_capability(
    iiagent_application_interface_runtime *r, const char *object_id, const char *id,
    const char *description, const char *input_schema, const char *output_schema,
    const char *const *permissions, size_t count, iiagent_application_interface_validator validator,
    iiagent_application_interface_handler handler, void *user) {
    return guarded([&] {
        if (!r || !object_id || !id || !description || !input_schema || !output_schema || !validator || !handler || (count && !permissions))
            return code(Status::invalid_argument);
        std::vector<std::string> names;
        for (size_t i = 0; i < count; ++i) {
            if (!permissions[i]) return code(Status::invalid_argument);
            names.emplace_back(permissions[i]);
        }
        return code(r->native.add_capability({object_id, id, description, input_schema, output_schema, std::move(names)},
            [validator, user](std::string_view input, std::string& reason) {
                const std::string copy(input);
                const char *message = nullptr;
                const int accepted = validator(user, copy.c_str(), &message);
                if (message) reason = message;
                return accepted != 0;
            },
            [handler, user](const AgentContext& context, std::string_view input) {
                const std::string copy(input);
                const char *result = nullptr;
                const int status = handler(user, context.principal.c_str(), copy.c_str(), &result);
                return Result{status == 0 ? Status::ok : Status::handler_failed, result ? result : ""};
            }));
    });
}
int iiagent_application_interface_remove_capability(iiagent_application_interface_runtime *r, const char *object_id, const char *id) {
    return guarded([&] { return !r || !object_id || !id ? code(Status::invalid_argument) : code(r->native.remove_capability(object_id, id)); });
}
int iiagent_application_interface_objects(iiagent_application_interface_runtime *r, const char *principal, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !out) return code(Status::invalid_argument);
        std::string json = "["; bool first = true;
        for (const auto& object : r->native.objects({principal})) {
            if (!first) json += ',';
            first = false; json += object_json(object);
        }
        return output_string(json + "]", out);
    });
}
int iiagent_application_interface_object(iiagent_application_interface_runtime *r, const char *principal, const char *id, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !id || !out) return code(Status::invalid_argument);
        auto object = r->native.object({principal}, id);
        return output_string(object ? object_json(*object) : "null", out);
    });
}
int iiagent_application_interface_capabilities(iiagent_application_interface_runtime *r, const char *principal, const char *id, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !id || !out) return code(Status::invalid_argument);
        std::string json = "["; bool first = true;
        for (const auto& capability : r->native.capabilities({principal}, id)) {
            if (!first) json += ',';
            first = false; json += capability_json(capability);
        }
        return output_string(json + "]", out);
    });
}
int iiagent_application_interface_procedure_authoring_context(
    iiagent_application_interface_runtime *r, const char *principal, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !out) return code(Status::invalid_argument);
        return output_string(authoring_json(r->native.procedure_authoring_context({principal})), out);
    });
}
int iiagent_application_interface_compose_procedure_source(
    iiagent_application_interface_runtime *r, const char *identifier, const char *preamble,
    const char *body, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !identifier || !preamble || !body || !out) return code(Status::invalid_argument);
        const auto result = compose_procedure_source({identifier, preamble, body});
        const auto written = output_string(result.value, out);
        return written == code(Status::ok) ? code(result.status) : written;
    });
}
int iiagent_application_interface_invoke(iiagent_application_interface_runtime *r, const char *principal, const char *object_id,
                   const char *capability_id, const char *input, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !object_id || !capability_id || !input || !out) return code(Status::invalid_argument);
        const auto result = r->native.invoke({principal}, object_id, capability_id, input);
        const auto written = output_string(result.value, out);
        return written == code(Status::ok) ? code(result.status) : written;
    });
}
int iiagent_application_interface_execute_procedure(
    iiagent_application_interface_runtime *r, const char *principal, const char *procedure_id,
    const char *input, iiagent_application_interface_handler procedure, void *user, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !procedure_id || !input || !procedure || !out)
            return code(Status::invalid_argument);
        const auto result = r->native.execute_procedure({principal}, procedure_id,
            [procedure, user](ProcedureContext& context, std::string_view text) {
                const std::string copy(text);
                const char *output = nullptr;
                const int status = procedure(user, context.subject().principal.c_str(), copy.c_str(), &output);
                return Result{status == 0 ? Status::ok : Status::handler_failed, output ? output : ""};
            }, input);
        const auto written = output_string(result.value, out);
        return written == code(Status::ok) ? code(result.status) : written;
    });
}
int iiagent_application_interface_execute_source(
    iiagent_application_interface_runtime *r, const char *principal, const char *procedure_id,
    const char *source, const iiagent_application_interface_source_backend *callbacks,
    const char *input, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !procedure_id || !source || !input || !out || !valid_backend(callbacks))
            return code(Status::invalid_argument);
        CallbackSourceBackend backend(*callbacks);
        const auto result = r->native.execute_source({principal}, procedure_id, source, backend, input);
        const auto written = output_string(result.value, out);
        return written == code(Status::ok) ? code(result.status) : written;
    });
}
int iiagent_application_interface_build_source(
    iiagent_application_interface_runtime *r, const char *principal, const char *identifier,
    const char *source, const iiagent_application_interface_source_backend *callbacks,
    iiagent_application_interface_compiled_procedure **out_procedure, char **out) {
    if (out) *out = nullptr;
    if (out_procedure) *out_procedure = nullptr;
    return guarded([&] {
        if (!r || !principal || !identifier || !source) return code(Status::invalid_argument);
        return create_compiled(callbacks, out_procedure, out, [&](SourceProcedureBackend& backend) {
            return r->native.build_source({principal}, identifier, source, backend);
        });
    });
}
int iiagent_application_interface_build_procedure(
    iiagent_application_interface_runtime *r, const char *principal, const char *identifier,
    const char *preamble, const char *body, const iiagent_application_interface_source_backend *callbacks,
    iiagent_application_interface_compiled_procedure **out_procedure, char **out) {
    if (out) *out = nullptr;
    if (out_procedure) *out_procedure = nullptr;
    return guarded([&] {
        if (!r || !principal || !identifier || !preamble || !body) return code(Status::invalid_argument);
        return create_compiled(callbacks, out_procedure, out, [&](SourceProcedureBackend& backend) {
            return r->native.build_procedure({principal}, {identifier, preamble, body}, backend);
        });
    });
}
int iiagent_application_interface_execute_built(iiagent_application_interface_runtime *r,
    const char *principal, iiagent_application_interface_compiled_procedure *procedure, const char *input, char **out) {
    if (out) *out = nullptr;
    return guarded([&] {
        if (!r || !principal || !procedure || !procedure->procedure || !input || !out)
            return code(Status::invalid_argument);
        const auto result = r->native.execute_built({principal}, *procedure->procedure, procedure->backend, input);
        const auto written = output_string(result.value, out);
        return written == code(Status::ok) ? code(result.status) : written;
    });
}
void iiagent_application_interface_destroy_compiled(iiagent_application_interface_compiled_procedure *procedure) {
    try { delete procedure; } catch (...) { /* C boundary */ }
}
uint64_t iiagent_application_interface_subscribe(
    iiagent_application_interface_runtime *r, const char *principal,
    iiagent_application_interface_observer observer, void *user) {
    try {
        if (!r || !principal || !observer) return 0;
        return r->native.subscribe({principal}, [observer, user](const Event& event) {
            observer(user, static_cast<int>(event.kind), event.object_id.c_str(), event.capability_id.c_str(), event.instance, event.revision);
        });
    } catch (...) { return 0; }
}
void iiagent_application_interface_unsubscribe(iiagent_application_interface_runtime *r, uint64_t subscription) {
    try { if (r) r->native.unsubscribe(subscription); } catch (...) { /* C boundary */ }
}

} // extern "C"
