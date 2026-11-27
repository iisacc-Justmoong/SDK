#pragma once

#include "Agent.h"
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace iiAgentApplicationInterface {

/// Host-owned command metadata. Schemas are JSON Schema documents and scopes
/// are labels interpreted only against separately supplied host grants.
struct ApplicationCommandDescriptor final {
    std::string name, target_id, description, input_schema, output_schema;
    std::vector<std::string> required_scopes;
    std::string capability_id; // Optional host command ID when tool name is an alias.
    bool opaque_input = false; // A legacy host receives arguments.input as text.
};
/// Value snapshot. principal must be filled from host authentication, never
/// trusted directly from a transport payload.
struct ApplicationCommandInvocation final {
    std::string request_id, command, target_id, arguments_json, principal;
    std::string capability_id;
};
/// One terminal result. A failure does not imply application-side rollback.
struct ApplicationCommandResult final {
    Status status = Status::ok;
    std::string output_json, diagnostic;
    bool cancelled = false;
};
/// Explicit trusted-host grant for one principal, target and command.
struct CommandGrant final {
    std::string principal, target_id, command;
    std::vector<std::string> scopes;
};
/// Resolution result contains identity only; object storage remains host-owned.
struct CommandTargetResolution final {
    Status status = Status::not_found;
    std::string object_id;
};
/// Match the requested exact target in a caller-supplied, already visible
/// snapshot. Empty, missing and duplicate identities never select a default.
struct CommandTargetResolver final {
    /// Read the caller-owned snapshot without retaining it; concurrent calls on
    /// immutable snapshots are safe. Missing and duplicate IDs are failures.
    [[nodiscard]] CommandTargetResolution execute(const ApplicationCommandInvocation&, std::span<const Object>) const;
};
/// Check a trusted grant against the complete invocation and descriptor. The
/// policy cannot authenticate the principal or replace Runtime authorization.
struct CommandScopePolicy final {
    /// Read only the supplied values. The grant must come from a trusted host;
    /// mismatched identities or a missing scope return false without side effects.
    [[nodiscard]] bool execute(const ApplicationCommandDescriptor&, const ApplicationCommandInvocation&, const CommandGrant&) const;
};
/// URI and schema document are data supplied by the caller, not implicit I/O.
struct CommandSchemaResource final { std::string uri, schema_json; };
/// Input and options for one validation; remote references resolve only from
/// resources. Format assertion is opt-in, as in the 2020-12 default dialect.
struct CommandSchemaValidationRequest final {
    std::string schema_json, instance_json;
    std::vector<CommandSchemaResource> resources;
    bool assert_formats = false;
    std::string default_dialect = "https://json-schema.org/draft/2020-12/schema";
};
/// Validation failures and malformed schemas are distinct from a valid value.
struct CommandSchemaValidationResult final {
    Status status = Status::internal_error;
    bool valid = false;
    std::string diagnostic;
};
/// Stateless schema validation. No filesystem, network, global state or
/// callbacks are accessed; concurrent calls with independent inputs are safe.
struct CommandSchemaValidator final {
    /// Validate one independent snapshot without retaining caller memory.
    /// Malformed, unresolved, unsupported-precision or unsupported-format input
    /// returns an error; no implicit external resource is accessed.
    [[nodiscard]] CommandSchemaValidationResult execute(const CommandSchemaValidationRequest&) const;
};
/// A host decision captured for publication. It is not a durable access grant.
struct CommandPublication final { ApplicationCommandDescriptor descriptor; bool allowed = false; };
/// Produce a detached list containing only explicitly approved descriptors.
struct CommandCapabilityPublisher final {
    /// Return owned copies of explicitly allowed values. The caller controls
    /// authorization freshness and can execute independent calls concurrently.
    [[nodiscard]] std::vector<ApplicationCommandDescriptor> execute(std::vector<CommandPublication>) const;
};
/// Thread-safe cooperative signal. This owns no worker and does not terminate a
/// host thread. The owner must keep the token and its associated task alive.
class CommandCancellationHandle final {
    std::stop_source source_;
public:
    /// Request cooperative stop exactly once, safely from any thread. This
    /// does not wait for work, destroy the handle, or roll back effects.
    [[nodiscard]] bool execute() noexcept { return source_.request_stop(); }
    /// Copy a token sharing the signal; the token keeps the signal alive.
    [[nodiscard]] std::stop_token token() const noexcept { return source_.get_token(); }
};
/// Process inputs supplied by the host. Arguments are literal, the working
/// directory must be absolute, and the environment is constructed solely from
/// these entries. The process backend itself is not part of the public header.
struct IsolatedProcessLaunch final {
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::map<std::string, std::optional<std::string>, std::less<>> environment;
};
/// Explicit process authority and buffers. Arguments are passed literally;
/// external effects of the child are owned by the host and cannot be rolled back.
struct IsolatedCommandRequest final {
    IsolatedProcessLaunch launch;
    std::string input;
    std::size_t output_limit = 8 * 1024 * 1024;
    std::size_t error_limit = 8 * 1024 * 1024;
};
/// Caller-owned worker and result. Destruction requests cancellation and joins
/// the worker; the cancellation source can also be signalled from another thread.
struct IsolatedCommandJob final {
    CommandCancellationHandle cancellation;
    std::future<std::uint64_t> started; // Process identity, or zero on launch failure.
    std::future<ApplicationCommandResult> result;
    std::jthread worker;
};
/// Orchestrate an isolated process in a background worker using iiSDKCore's
/// owned process tree. Cancelling kills that tree, not the application host.
/// The request is copied into the worker and the returned job owns its lifetime.
[[nodiscard]] IsolatedCommandJob start_isolated_command(IsolatedCommandRequest, CommandCancellationHandle = {});
/// The CLI parser returns data only; the host authenticates and executes it.
struct CliCommandOutput final {
    Status status = Status::invalid_argument;
    ApplicationCommandInvocation invocation;
    std::string diagnostic;
};
/// Parse a literal argument vector; duplicate, unknown and missing flags fail.
struct CliCommandAdapter final {
    /// Return a detached invocation or a diagnostic, without contacting a host.
    /// The argument vector must remain valid only for the duration of this call.
    [[nodiscard]] CliCommandOutput execute(std::vector<std::string>) const;
};
/// An adapter operation is requested explicitly by the external orchestrator.
enum class McpCommandOperation { request, result, progress };
struct McpCommandInput final {
    McpCommandOperation operation = McpCommandOperation::request;
    std::string message, request_id_json;
    std::vector<ApplicationCommandDescriptor> published;
    ApplicationCommandResult result;
    CommandProgressEvent progress;
};
/// A request can produce a response, a host invocation, or a cancellation
/// identity. The host owns authentication, execution and transport lifecycle.
struct McpCommandOutput final {
    Status status = Status::ok;
    std::string response, request_id_json, cancellation_id_json, progress_token_json;
    std::optional<ApplicationCommandInvocation> invocation;
};
/// Translate MCP JSON-RPC requests/results without executing a command. The
/// caller supplies an authorization-filtered command snapshot on each request.
struct McpCommandAdapter final {
    /// Convert one bounded JSON-RPC message or one supplied event to owned
    /// output. Invalid metadata never yields an executable invocation. The
    /// host owns authorization, callbacks, ordering and transport lifetime.
    [[nodiscard]] McpCommandOutput execute(const McpCommandInput&) const;
};

} // namespace iiAgentApplicationInterface
