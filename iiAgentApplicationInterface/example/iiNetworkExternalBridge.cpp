#include "iiNetworkExternalBridge.h"

#include <iiJson/Json.h>
#include <map>
#include <set>
#include <utility>

namespace iiAgentApplicationInterface::integration {
namespace {
using Value = ii::json::Value;
using JsonObject = Value::Object;
using JsonArray = Value::Array;

/// Copy an optional JSON text member without coercing non-string values.
bool text_field(const JsonObject& object, std::string_view name, std::string& output, bool required = false) {
    auto found = object.find(name);
    if (found == object.end()) return !required;
    auto text = found->second.get<std::string>();
    if (!text) return false;
    output = text->get();
    return output.find('\0') == std::string::npos;
}

/// Decode the small versioned agent request without trusting caller-supplied identities.
std::optional<ExternalRequest> decode_request(std::string_view source, std::size_t limit) {
    ii::json::Limits limits;
    limits.input_bytes = limit; limits.string_bytes = limit; limits.items = 64; limits.depth = 4;
    auto parsed = ii::json::parse(source, limits);
    if (!parsed) return std::nullopt;
    auto body = parsed->get<JsonObject>();
    if (!body) return std::nullopt;
    const auto& fields = body->get();
    static const std::set<std::string, std::less<>> allowed{
        "protocol", "operation", "credential", "object_id", "capability_id", "cursor", "limit", "input", "procedure_id", "source", "preamble", "body"};
    for (const auto& [name, value] : fields) { (void)value; if (!allowed.contains(name)) return std::nullopt; }
    std::string protocol, operation;
    ExternalRequest request;
    if (!text_field(fields, "protocol", protocol, true) || protocol != "iiagent.application.v1" ||
        !text_field(fields, "operation", operation, true) || !text_field(fields, "credential", request.credential) ||
        !text_field(fields, "object_id", request.object_id) || !text_field(fields, "capability_id", request.capability_id) ||
        !text_field(fields, "cursor", request.cursor) || !text_field(fields, "input", request.input) ||
        !text_field(fields, "procedure_id", request.procedure_id) || !text_field(fields, "source", request.source) ||
        !text_field(fields, "preamble", request.preamble) || !text_field(fields, "body", request.body)) return std::nullopt;
    if (auto found = fields.find("limit"); found != fields.end()) {
        if (auto number = found->second.get<std::uint64_t>(); number && number->get() <= 100)
            request.limit = static_cast<std::size_t>(number->get());
        else if (auto number = found->second.get<std::int64_t>(); number && number->get() >= 0 && number->get() <= 100)
            request.limit = static_cast<std::size_t>(number->get());
        else return std::nullopt;
    }
    if (operation == "describe") request.operation = ExternalOperation::describe;
    else if (operation == "list_objects") request.operation = ExternalOperation::list_objects;
    else if (operation == "read_object") request.operation = ExternalOperation::read_object;
    else if (operation == "list_capabilities") request.operation = ExternalOperation::list_capabilities;
    else if (operation == "invoke") request.operation = ExternalOperation::invoke;
    else if (operation == "execute_source") request.operation = ExternalOperation::execute_source;
    else if (operation == "execute_draft") request.operation = ExternalOperation::execute_draft;
    else return std::nullopt;
    return request;
}

/// Project one state-free object header to a structured JSON value.
Value header_json(const ObjectHeader& item) {
    return Value(JsonObject{{"id", Value(item.id)}, {"type", Value(item.type)}, {"description", Value(item.description)},
        {"instance", Value(item.instance)}, {"revision", Value(item.revision)}});
}

/// Project one separately requested object including its host-published state.
Value object_json(const Object& item) {
    return Value(JsonObject{{"id", Value(item.id)}, {"type", Value(item.type)}, {"description", Value(item.description)},
        {"state", Value(item.state)}, {"instance", Value(item.instance)}, {"revision", Value(item.revision)}});
}

/// Project one authorized command and its opaque schema descriptions.
Value capability_json(const Capability& item) {
    JsonArray permissions;
    for (const auto& permission : item.permissions) permissions.emplace_back(permission);
    return Value(JsonObject{{"object_id", Value(item.object_id)}, {"id", Value(item.id)},
        {"description", Value(item.description)}, {"input_schema", Value(item.input_schema)},
        {"output_schema", Value(item.output_schema)}, {"permissions", Value(std::move(permissions))}});
}

/// Encode only the data actually returned by the authenticated gateway operation.
std::string encode_response(const ExternalResponse& response, std::size_t limit) {
    try {
        JsonArray objects, capabilities, operations;
        for (const auto& object : response.objects) objects.push_back(header_json(object));
        for (const auto& capability : response.capabilities) capabilities.push_back(capability_json(capability));
        for (const auto& operation : response.operations) {
            JsonArray required, optional;
            for (const auto& name : operation.required) required.emplace_back(name);
            for (const auto& name : operation.optional) optional.emplace_back(name);
            operations.emplace_back(JsonObject{{"name", Value(operation.name)}, {"description", Value(operation.description)},
                {"required", Value(std::move(required))}, {"optional", Value(std::move(optional))}});
        }
        JsonObject output{{"protocol", Value("iiagent.application.v1")},
            {"status", Value(static_cast<int>(response.status))}, {"value", Value(response.value)},
            {"objects", Value(std::move(objects))}, {"next_cursor", Value(response.next_cursor)},
            {"capabilities", Value(std::move(capabilities))}, {"operations", Value(std::move(operations))},
            {"object", response.object ? object_json(*response.object) : Value()}};
        ii::json::Limits limits;
        limits.output_bytes = limit - 1; limits.string_bytes = limit - 1;
        auto encoded = ii::json::stringify(Value(std::move(output)), limits);
        if (encoded) return *encoded + "\n";
    } catch (...) { /* Do not expose host diagnostics or malformed state over the wire. */ }
    return "{\"protocol\":\"iiagent.application.v1\",\"status\":7,\"value\":\"response unavailable\"}\n";
}
}

/// Shared owner-loop state keeps asynchronous socket callbacks safe during shutdown.
struct LocalAgentBridge::State : std::enable_shared_from_this<State> {
    /// One bounded connection handles one authenticated request and drains one response.
    struct Peer : std::enable_shared_from_this<Peer> {
        std::weak_ptr<State> owner;
        std::uint64_t id;
        std::unique_ptr<ii::network::LocalSocket> stream;
        std::string input;
        bool answered = false;
        Peer(std::weak_ptr<State> state, std::uint64_t key, std::unique_ptr<ii::network::LocalSocket> socket)
            : owner(std::move(state)), id(key), stream(std::move(socket)) {}
        /// Consume one newline-framed request and pass transport-supplied peer credentials to the gateway.
        void receive() {
            auto state = owner.lock();
            if (!state || !state->alive || answered) return;
            try {
                auto bytes = stream->read();
                if (bytes.size() > state->options.max_request_bytes - input.size()) { state->remove(id); return; }
                input += bytes;
                const auto end = input.find('\n');
                if (end == std::string::npos) return;
                answered = true;
                ExternalResponse response{Status::invalid_argument, "invalid request"};
                if (end + 1 == input.size()) {
                    auto request = decode_request(std::string_view(input).substr(0, end), state->options.max_request_bytes);
                    if (request) {
                        ExternalPeer identity;
                        if (auto peer = stream->peer_credentials()) {
                            identity.user_id = peer->user; identity.group_id = peer->group; identity.process_id = peer->process;
                        }
                        response = state->gateway->handle(identity, *request);
                    }
                }
                input.clear();
                const auto reply = encode_response(response, state->options.max_response_bytes);
                if (!stream->write(reply)) { state->remove(id); return; }
                stream->close(true);
                if (!stream->is_connected()) state->remove(id);
            } catch (...) { state->remove(id); }
        }
    };
    ExternalGateway* gateway;
    LocalBridgeOptions options;
    ii::network::LocalServer server;
    std::map<std::uint64_t, std::shared_ptr<Peer>> peers;
    std::uint64_t next = 1;
    bool alive = true;
    State(ii::runtime::EventLoop& owner, ExternalGateway& service, LocalBridgeOptions value)
        : gateway(&service), options(value), server(owner) {}
    /// Attach weak callbacks after a peer has a stable shared lifetime.
    void accept(std::unique_ptr<ii::network::LocalSocket> socket) {
        if (!alive || peers.size() >= options.max_connections) { socket->close(); return; }
        try {
            const auto id = next++;
            auto peer = std::make_shared<Peer>(weak_from_this(), id, std::move(socket));
            const std::weak_ptr<Peer> weak = peer;
            peer->stream->readable = [weak] { if (auto keep = weak.lock()) keep->receive(); };
            peer->stream->failed = [weak](std::string) { if (auto keep = weak.lock()) if (auto state = keep->owner.lock()) state->remove(keep->id); };
            peer->stream->disconnected = [weak] { if (auto keep = weak.lock()) if (auto state = keep->owner.lock()) state->remove(keep->id); };
            peers.emplace(id, std::move(peer));
        } catch (...) { if (socket) socket->close(); }
    }
    /// Release a peer outside transport-owned storage; the callback holds a temporary shared owner.
    void remove(std::uint64_t id) { peers.erase(id); }
};

/// Connect independent SDK interfaces in application-owned adapter code.
LocalAgentBridge::LocalAgentBridge(ii::runtime::EventLoop& loop, ExternalGateway& gateway, LocalBridgeOptions options)
    : state_(std::make_shared<State>(loop, gateway, options)) {
    std::weak_ptr<State> weak = state_;
    state_->server.accepted = [weak](auto socket) { if (auto state = weak.lock()) state->accept(std::move(socket)); };
}
/// Close the listener before releasing peer state.
LocalAgentBridge::~LocalAgentBridge() { close(); }
/// Bind an app-owned endpoint with restrictive iiNetwork local socket defaults.
bool LocalAgentBridge::listen(std::string_view endpoint, std::string& diagnostic) {
    if (!state_->alive || !state_->options.max_request_bytes || state_->options.max_response_bytes < 128 ||
        !state_->options.max_connections || state_->options.idle_timeout.count() <= 0) {
        diagnostic = "invalid bridge limits"; return false;
    }
    ii::network::LocalServerOptions options;
    options.stream.receive_bytes = state_->options.max_request_bytes;
    options.stream.pending_bytes = state_->options.max_response_bytes;
    options.stream.idle_timeout = state_->options.idle_timeout;
    auto listening = state_->server.listen({std::string(endpoint)}, options);
    if (!listening) { diagnostic = listening.error().message; return false; }
    diagnostic.clear(); return true;
}
/// Cancel callbacks, release all peers, and remove the endpoint owned by iiNetwork.
void LocalAgentBridge::close() {
    if (!state_ || !state_->alive) return;
    state_->alive = false;
    state_->server.close();
    state_->peers.clear();
}

} // namespace iiAgentApplicationInterface::integration
