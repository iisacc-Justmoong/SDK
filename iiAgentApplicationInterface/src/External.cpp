#include "External.h"

#include <utility>

namespace iiAgentApplicationInterface {

/// Retain non-owning application services while owning only the authentication callback.
ExternalGateway::ExternalGateway(Runtime& runtime, ExternalAuthenticator authenticator, SourceProcedureBackend* backend)
    : runtime_(&runtime), authenticator_(std::move(authenticator)), backend_(backend) {}

/// Verify the external agent, then route one bounded request to the live application runtime.
ExternalResponse ExternalGateway::handle(const ExternalPeer& peer, const ExternalRequest& request) {
    if (!runtime_->is_owner_thread()) return {Status::invalid_argument, "runtime owner thread required"};
    std::optional<AgentContext> agent;
    try { if (authenticator_) agent = authenticator_(peer, request.credential); }
    catch (...) { return {Status::denied, "authentication failed"}; }
    if (!agent || agent->principal.empty()) return {Status::denied, "authentication failed"};
    switch (request.operation) {
    case ExternalOperation::describe: {
        ExternalResponse result;
        result.value = "iiAgentApplicationInterface.external.v1";
        result.operations = {
            {"describe", "List this compact interface without application state.", {}, {}},
            {"list_objects", "List authorized object metadata without state, with a lexicographic cursor.", {}, {"cursor", "limit"}},
            {"read_object", "Read one authorized object's host-published state snapshot.", {"object_id"}, {}},
            {"list_capabilities", "Page through discoverable commands and schemas for one readable object.", {"object_id"}, {"cursor", "limit"}},
            {"invoke", "Run an authorized application command with validated text input.", {"object_id", "capability_id"}, {"input"}}
        };
        if (backend_) {
            result.operations.push_back({"execute_source", "Compile and run a trusted complete C++23 module in the app.",
                                         {"procedure_id", "source"}, {"input"}});
            result.operations.push_back({"execute_draft", "Run a trusted C++23 body with ProcedureContext& context and std::string_view input; context.invoke(object_id, capability_id, input) returns Result. context.read_object<T>(id) and context.write_object<T>(id) pin an authorized live object of exact type T; supply its application header in preamble.",
                                         {"procedure_id", "body"}, {"preamble", "input"}});
        }
        return result;
    }
    case ExternalOperation::list_objects: {
        auto page = runtime_->object_page(*agent, request.cursor, request.limit);
        ExternalResponse result;
        result.status = page.status;
        result.objects = std::move(page.objects);
        result.next_cursor = std::move(page.next_cursor);
        return result;
    }
    case ExternalOperation::read_object: {
        if (request.object_id.empty()) return {Status::invalid_argument, "object_id is required"};
        ExternalResponse result;
        result.object = runtime_->object(*agent, request.object_id);
        if (!result.object) { result.status = Status::denied; result.value = "object unavailable"; }
        return result;
    }
    case ExternalOperation::list_capabilities: {
        if (request.object_id.empty()) return {Status::invalid_argument, "object_id is required"};
        auto page = runtime_->capability_page(*agent, request.object_id, request.cursor, request.limit);
        ExternalResponse result;
        result.status = page.status;
        result.capabilities = std::move(page.capabilities);
        result.next_cursor = std::move(page.next_cursor);
        return result;
    }
    case ExternalOperation::invoke: {
        if (request.object_id.empty() || request.capability_id.empty())
            return {Status::invalid_argument, "object_id and capability_id are required"};
        auto result = runtime_->invoke(*agent, request.object_id, request.capability_id, request.input);
        return {result.status, std::move(result.value)};
    }
    case ExternalOperation::execute_source: {
        if (!backend_) return {Status::handler_failed, "source backend unavailable"};
        auto result = runtime_->execute_source(*agent, request.procedure_id, request.source, *backend_, request.input);
        return {result.status, std::move(result.value)};
    }
    case ExternalOperation::execute_draft: {
        if (!backend_) return {Status::handler_failed, "source backend unavailable"};
        auto source = compose_procedure_source({request.procedure_id, request.preamble, request.body});
        if (source.status != Status::ok) return {source.status, std::move(source.value)};
        auto result = runtime_->execute_source(*agent, request.procedure_id, source.value, *backend_, request.input);
        return {result.status, std::move(result.value)};
    }
    }
    return {Status::invalid_argument, "unsupported operation"};
}

} // namespace iiAgentApplicationInterface
