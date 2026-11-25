#pragma once

#include <iiAgentApplicationInterface>
#include <iiNetwork/Local.h>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace iiAgentApplicationInterface::integration {

/// Host-chosen bounds for one local agent endpoint; the application owns its endpoint directory.
struct LocalBridgeOptions {
    std::size_t max_request_bytes = 1024 * 1024;
    std::size_t max_response_bytes = 1024 * 1024;
    std::size_t max_connections = 32;
    std::chrono::milliseconds idle_timeout{30000};
};

/// Application-side adapter joining the independent agent, networking and JSON SDK interfaces.
/// Construct, listen and destroy on the event loop thread that owns the supplied runtime.
class LocalAgentBridge final {
public:
    /// Borrow the running application's loop and gateway, which must both outlive the bridge.
    LocalAgentBridge(ii::runtime::EventLoop& loop, ExternalGateway& gateway, LocalBridgeOptions options = {});
    /// Stop listening and close all active peer connections.
    ~LocalAgentBridge();
    LocalAgentBridge(const LocalAgentBridge&) = delete;
    LocalAgentBridge& operator=(const LocalAgentBridge&) = delete;
    /// Listen on an app-owned local endpoint; a failure populates a non-sensitive diagnostic.
    [[nodiscard]] bool listen(std::string_view endpoint, std::string& diagnostic);
    /// Disconnect clients and release only the local endpoint owned by this server.
    void close();
private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace iiAgentApplicationInterface::integration
