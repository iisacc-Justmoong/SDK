#pragma once

#include "Agent.h"
#include "SourceProcedure.h"

namespace iiAgentApplicationInterface {

/// OS-authenticated peer attributes supplied by the host transport, never by the request payload.
struct ExternalPeer {
    /// Operating-system user identity, when the transport can obtain it.
    std::optional<std::uint64_t> user_id;
    /// Operating-system group identity, when the transport can obtain it.
    std::optional<std::uint64_t> group_id;
    /// Operating-system process identity, when the transport can obtain it.
    std::optional<std::uint64_t> process_id;
};

/// Operations supported by the transport-independent external application interface.
enum class ExternalOperation { describe, list_objects, read_object, list_capabilities, invoke, execute_source, execute_draft };

/// A single untrusted request; the credential is verified by the host on every call.
struct ExternalRequest {
    ExternalOperation operation = ExternalOperation::describe;
    std::string credential;
    std::string object_id;
    std::string capability_id;
    std::string cursor;
    std::string input;
    std::string procedure_id;
    std::string source;
    std::string preamble;
    std::string body;
    std::size_t limit = 32;
};

/// One discoverable external operation and its required and optional request fields.
struct ExternalOperationInfo {
    std::string name;
    std::string description;
    std::vector<std::string> required;
    std::vector<std::string> optional;
};

/// Authorized result for one external request. Object pages never contain object state.
struct ExternalResponse {
    Status status = Status::ok;
    std::string value;
    std::vector<ObjectHeader> objects;
    std::string next_cursor;
    std::optional<Object> object;
    std::vector<Capability> capabilities;
    std::vector<ExternalOperationInfo> operations;
};

/// Host authentication maps verified credentials and transport peer identity to an agent principal.
/// A null result or exception denies the request. No peer field is proof by itself.
using ExternalAuthenticator = std::function<std::optional<AgentContext>(const ExternalPeer&, std::string_view)>;

/// In-process request dispatcher for an externally connected, host-authenticated agent.
/// The host provides transport and calls handle only on the runtime's serialized owner thread.
class ExternalGateway final {
public:
    /// Connect a live runtime, explicit authenticator and optional host-owned compiler/loader.
    /// All three must outlive the gateway and its active requests.
    ExternalGateway(Runtime& runtime, ExternalAuthenticator authenticator, SourceProcedureBackend* backend = nullptr);
    /// Authenticate again, then apply the runtime's existing object and command policies.
    /// Source execution is synchronous and runs native code with application privileges.
    [[nodiscard]] ExternalResponse handle(const ExternalPeer& peer, const ExternalRequest& request);
private:
    Runtime* runtime_;
    ExternalAuthenticator authenticator_;
    SourceProcedureBackend* backend_;
};

} // namespace iiAgentApplicationInterface
