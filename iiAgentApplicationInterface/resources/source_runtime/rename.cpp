#include <iiAgentApplicationInterface>

namespace {
/// A source-authored procedure that invokes the application's registered command.
iiAgentApplicationInterface::Result run(iiAgentApplicationInterface::ProcedureContext& context,
                                        std::string_view text) {
    return context.invoke("document", "rename", text);
}
}

/// Return the standard descriptor understood by the host loader.
extern "C" const iiAgentApplicationInterface::NativeProcedureDescriptor*
iiagent_application_procedure_v1() noexcept {
    static const iiAgentApplicationInterface::NativeProcedureDescriptor descriptor{
        sizeof(iiAgentApplicationInterface::NativeProcedureDescriptor),
        iiAgentApplicationInterface::native_procedure_descriptor_version, "source.rename", &run};
    return &descriptor;
}
