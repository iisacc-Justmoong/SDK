#include <iiAgentApplicationInterface>
#include "Application.h"

namespace {
/// An agent-authored source procedure compiled separately from the host application.
iiAgentApplicationInterface::Result run_procedure(
    iiAgentApplicationInterface::ProcedureContext& context, std::string_view title) {
    auto application = context.write_object<native_procedure_fixture::Application>("application");
    if (!application) return {iiAgentApplicationInterface::Status::denied, "application unavailable"};
    application->rename(std::string(title));
    return context.invoke("application", "publish-title", application->title());
}
}

/// Export the shared descriptor entry point for the host's chosen module loader.
extern "C" const iiAgentApplicationInterface::NativeProcedureDescriptor*
iiagent_application_procedure_v1() noexcept {
    static const iiAgentApplicationInterface::NativeProcedureDescriptor descriptor{
        sizeof(iiAgentApplicationInterface::NativeProcedureDescriptor),
        iiAgentApplicationInterface::native_procedure_descriptor_version,
        "agent.authored", &run_procedure};
    return &descriptor;
}
