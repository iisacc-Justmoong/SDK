#include <iiAgentApplicationInterface>
#include <iiAgentApplicationInterface/AgentC.h>

/// Verify the installed CMake target exposes both entry points without source paths.
int main() {
    iiAgentApplicationInterface::Runtime runtime([](const iiAgentApplicationInterface::AccessRequest&) { return true; });
    if (runtime.add_object({"installed", "document", "", "state"}) != iiAgentApplicationInterface::Status::ok) return 1;
    auto value = runtime.object({"test"}, "installed");
    auto validation = iiAgentApplicationInterface::CommandSchemaValidator{}.execute({R"({"type":"integer"})", "7"});
    return value && value->state == "state" && validation.valid && IIAGENTAPPLICATIONINTERFACE_OK == 0 ? 0 : 1;
}
