#include <iiAgentApplicationInterface>

#include <string>
#include <string_view>

/// Build one in-process host integration without a GUI or external agent transport.
int main() {
    // A real application authenticates identities before constructing these contexts.
    iiAgentApplicationInterface::Runtime runtime([](const iiAgentApplicationInterface::AccessRequest& request) {
        return request.context.principal == "local-user" || request.context.principal == "local-agent";
    });
    if (runtime.add_object({"document.1", "document", "Example document", "Draft"}) != iiAgentApplicationInterface::Status::ok) return 1;
    const auto registered = runtime.add_capability(
        {"document.1", "rename", "Rename a document", "non-empty text", "text", {"document.write"}},
        [](std::string_view text, std::string& reason) {
            reason = "The name cannot be empty";
            return !text.empty();
        },
        [&runtime](const iiAgentApplicationInterface::AgentContext&, std::string_view name) {
            const auto status = runtime.update_state("document.1", std::string(name));
            return iiAgentApplicationInterface::Result{status, status == iiAgentApplicationInterface::Status::ok ? "renamed" : "document unavailable"};
        });
    if (registered != iiAgentApplicationInterface::Status::ok) return 1;
    // UI and agent adapters call the same command; the host supplies authenticated identities.
    const auto from_ui = runtime.invoke({"local-user"}, "document.1", "rename", "Notes");
    const auto from_agent = runtime.invoke({"local-agent"}, "document.1", "rename", "Work Notes");
    return from_ui.status == iiAgentApplicationInterface::Status::ok && from_agent.status == iiAgentApplicationInterface::Status::ok ? 0 : 1;
}
