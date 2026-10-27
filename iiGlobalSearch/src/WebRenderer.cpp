#include "WebRenderer.h"
#include <iiFileProviderNative.h>
#include <iiSDKCore/Core/Time/Deadline.h>
#include <iiSDKCore/Process/Process.h>
#include <iiNetwork/Http.h>
#include <iiNetwork/Transport.h>
#include <iiNetwork/WebSocket.h>
#include <iiJson/Json.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace iiGlobalSearch::detail {
namespace fs = std::filesystem;
namespace {
std::string text(const fs::path& path) {
    const auto u = path.u8string(); return {reinterpret_cast<const char*>(u.data()), u.size()};
}
fs::path browserPath(fs::path configured) {
    if (!configured.empty()) return fs::absolute(configured);
#ifdef __APPLE__
    const fs::path chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
    if (fs::is_regular_file(chrome)) return chrome;
#elif defined(_WIN32)
    for (const char* key : {"PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"}) {
        if (const auto* base = std::getenv(key)) {
            auto candidate = fs::path(base) / "Google/Chrome/Application/chrome.exe";
            if (fs::is_regular_file(candidate)) return candidate;
        }
    }
#else
    if (const auto* path = std::getenv("PATH")) {
        std::string paths(path); std::size_t start = 0;
        while (start <= paths.size()) {
            const auto end = paths.find(':', start);
            const auto directory = paths.substr(start, end == paths.npos ? paths.npos : end - start);
            if (!directory.empty()) for (const char* name : {"google-chrome", "chromium", "chromium-browser"}) {
                auto candidate = fs::path(directory) / name;
                if (fs::is_regular_file(candidate)) return fs::absolute(candidate);
            }
            if (end == paths.npos) break;
            start = end + 1;
        }
    }
#endif
    return {};
}
class ChromiumRenderer final : public WebRenderer {
public:
    explicit ChromiumRenderer(const WebConfiguration& config)
        : executable(browserPath(config.browserExecutable)), scratch(config.scratchDirectory.empty()
              ? fs::current_path() / "build/web-rendering" : fs::absolute(config.scratchDirectory)) {}

    std::expected<WebRenderedDocument, std::string> render(const std::string& url,
        std::chrono::milliseconds timeout, std::chrono::milliseconds settle,
        std::size_t maximum, std::stop_token stop) override {
        try {
            if (executable.empty() || !fs::is_regular_file(executable)) return std::unexpected("Chromium browser executable is unavailable");
            if (stop.stop_requested()) return std::unexpected("Rendering cancelled");
            iiFileProvider::native::TemporaryDirectory profile(scratch);
            ii::process::Launch launch;
            launch.arguments = {text(executable), "--headless=new", "--incognito", "--disable-gpu", "--no-first-run",
                "--no-default-browser-check", "--disable-background-networking", "--disable-sync", "--disable-extensions",
                "--disable-default-apps", "--disable-component-update", "--disable-notifications", "--disable-application-cache",
                "--disable-breakpad", "--disable-crash-reporter",
                "--user-agent=iiGlobalSearch/0.1 (+https://iisacc.com/)",
                "--media-cache-size=1", "--disk-cache-size=1", "--password-store=basic", "--use-mock-keychain",
                "--user-data-dir=" + text(profile.path()), "--remote-debugging-port=0",
                "--remote-allow-origins=http://127.0.0.1", "about:blank"};
            launch.input = ii::process::Input::closed; launch.output = ii::process::Output::discard;
            launch.error = ii::process::ErrorOutput::discard;
            launch.environment["TMPDIR"] = text(profile.path());
            launch.environment["TEMP"] = text(profile.path());
            launch.environment["TMP"] = text(profile.path());
            auto process = ii::process::Process::start(launch);
            if (!process) return std::unexpected("Cannot launch Chromium: " + process.error().message);
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            unsigned port = 0;
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                std::ifstream info(profile.path() / "DevToolsActivePort");
                if (info >> port && port > 0 && port < 65536) break;
                auto exited = process->poll();
                if (!exited || *exited) return std::unexpected("Chromium exited before its renderer became available");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (stop.stop_requested()) return std::unexpected("Rendering cancelled");
            if (!port) return std::unexpected("Chromium startup timed out");

            ii::runtime::EventLoop loop;
            ii::network::Http discovery(loop); ii::network::HttpRequest request;
            request.url = "http://127.0.0.1:" + std::to_string(port) + "/json/list";
            request.proxy.type = ii::network::ProxyType::disabled; request.timeout = timeout; request.maximum_bytes = 1024 * 1024;
            bool ready = false; ii::network::HttpResponse listing;
            if (!discovery.request(std::move(request), [&](auto value) { listing = std::move(value); ready = true; }))
                return std::unexpected("Cannot request Chromium page list");
            while (!ready && !stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                loop.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (!ready || !listing.ok()) { discovery.cancel(); return std::unexpected("Cannot read Chromium page list"); }
            auto parsed = ii::json::parse(listing.body);
            if (!parsed) return std::unexpected("Chromium page list is invalid");
            auto array = parsed->get<ii::json::Value::Array>();
            if (!array) return std::unexpected("Chromium page list is not an array");
            std::string ws;
            for (const auto& page : array->get()) {
                if (fieldText(page, "type") == "page" && fieldText(page, "url") == "about:blank") {
                    ws = fieldText(page, "webSocketDebuggerUrl"); break;
                }
            }
            const auto prefix = "ws://127.0.0.1:" + std::to_string(port);
            if (!ws.starts_with(prefix + "/devtools/page/")) return std::unexpected("Chromium page debugger is unavailable");

            ii::network::WebSocketClient client;
            client.host = "127.0.0.1:" + std::to_string(port); client.origin = "http://127.0.0.1";
            client.target = ws.substr(prefix.size());
            ii::network::WebSocketLimits limits;
            limits.message_bytes = maximum * 6 + 65536; limits.buffered_bytes = limits.message_bytes + 65536;
            limits.output_bytes = 128 * 1024;
            auto socketProtocol = ii::network::WebSocketSession::client(client, limits);
            if (!socketProtocol) return std::unexpected("Cannot create Chromium debugger session");
            ii::network::Tcp socket(loop); socket.receive_limit(limits.buffered_bytes);
            if (!socket.connect("127.0.0.1", static_cast<std::uint16_t>(port))) return std::unexpected("Cannot connect to Chromium debugger");
            bool handshake = false, enabled = false, navigated = false, evaluation = false, done = false;
            std::optional<std::chrono::steady_clock::time_point> loaded;
            std::string error; WebRenderedDocument result;
            auto send = [&](int id, const char* method, ii::json::Value::Object parameters) {
                ii::json::Value message(ii::json::Value::Object{{"id", ii::json::Value(id)},
                    {"method", ii::json::Value(method)}, {"params", ii::json::Value(std::move(parameters))}});
                auto encoded = ii::json::stringify(message);
                if (!encoded || !socketProtocol->send_text(*encoded) || !socket.write(socketProtocol->take_output()))
                    throw std::runtime_error("Cannot send Chromium debugger command");
            };
            while (!done && !stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                loop.poll();
                if (socket.is_connected() && !handshake) { if (!socket.write(socketProtocol->take_output())) throw std::runtime_error("Cannot start debugger handshake"); handshake = true; }
                if (socket.available_bytes()) {
                    auto events = socketProtocol->receive(socket.read());
                    if (!events) throw std::runtime_error("Chromium debugger response failed");
                    for (const auto& event : *events) {
                        if (event.kind == ii::network::WebSocketEventKind::opened && !enabled) {
                            send(1, "Page.enable", {}); enabled = true;
                        } else if (event.kind == ii::network::WebSocketEventKind::text) {
                            ii::json::Limits jsonLimits; jsonLimits.input_bytes = maximum * 6 + 65536; jsonLimits.string_bytes = maximum + 65536;
                            auto message = ii::json::parse(event.payload, jsonLimits);
                            if (!message) throw std::runtime_error("Chromium debugger returned invalid JSON");
                            const auto id = fieldNumber(*message, "id");
                            if (id == 1 && !navigated) { send(2, "Page.navigate", {{"url", ii::json::Value(url)}}); navigated = true; }
                            else if (id == 2) {
                                if (const auto* response = field(*message, "result"); response && !fieldText(*response, "errorText").empty())
                                    throw std::runtime_error("Chromium could not navigate to the page");
                            } else if (id == 3) {
                                const auto* response = field(*message, "result");
                                const auto* remote = response ? field(*response, "result") : nullptr;
                                const auto* value = remote ? field(*remote, "value") : nullptr;
                                if (!value) throw std::runtime_error("Chromium returned no DOM");
                                result.finalUrl = fieldText(*value, "url"); result.html = fieldText(*value, "html");
                                if (result.html.empty() || result.html.size() > maximum) throw std::runtime_error("Chromium DOM is empty or exceeds its limit");
                                done = true;
                            } else if (navigated && fieldText(*message, "method") == "Page.loadEventFired") loaded = std::chrono::steady_clock::now();
                        }
                    }
                    const auto pending = socketProtocol->take_output();
                    if (!pending.empty() && !socket.write(pending)) throw std::runtime_error("Cannot send debugger control frame");
                }
                if (loaded && !evaluation && std::chrono::steady_clock::now() >= *loaded + settle) {
                    send(3, "Runtime.evaluate", {{"expression", ii::json::Value("({url: location.href, html: document.documentElement ? document.documentElement.outerHTML : ''})")},
                        {"returnByValue", ii::json::Value(true)}});
                    evaluation = true;
                }
                if (socket.state() == ii::network::SocketState::failed || socket.state() == ii::network::SocketState::closed)
                    throw std::runtime_error("Chromium debugger disconnected");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            socket.close();
            (void)process->cancel();
            if (stop.stop_requested()) return std::unexpected("Rendering cancelled");
            if (!done) return std::unexpected("Chromium rendering timed out");
            return result;
        } catch (const std::exception& e) { return std::unexpected(e.what()); }
    }
private:
    static const ii::json::Value* field(const ii::json::Value& value, std::string_view name) {
        auto found = value.find(name); return found ? &found->get() : nullptr;
    }
    static std::string fieldText(const ii::json::Value& value, std::string_view name) {
        auto* item = field(value, name); if (!item) return {};
        auto str = item->get<std::string>(); return str ? str->get() : std::string{};
    }
    static std::int64_t fieldNumber(const ii::json::Value& value, std::string_view name) {
        auto* item = field(value, name); if (!item) return 0;
        if (auto n = item->get<std::int64_t>()) return n->get();
        if (auto n = item->get<std::uint64_t>()) return static_cast<std::int64_t>(n->get());
        return 0;
    }
    fs::path executable, scratch;
};
}
std::shared_ptr<WebRenderer> browserRenderer(const WebConfiguration& config) {
    return config.renderer ? config.renderer : std::make_shared<ChromiumRenderer>(config);
}
} // namespace iiGlobalSearch::detail
