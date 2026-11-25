#include <iiAgentApplicationInterface>
#include "model.hpp"

/// Verify that the generated catalog reaches the application's own runtime.
int main() {
    auto runtime = iiAgentApplicationInterface::generated::create_application_runtime(
        [](const iiAgentApplicationInterface::AccessRequest&) { return true; });
    if (!runtime) return 1;
    const auto catalog = runtime->object({"reader"}, "application.build_catalog");
    if (!catalog) return 2;
    return catalog->state.find("demo::Project::secret") != std::string::npos &&
           catalog->state.find("demo::Note::archive") != std::string::npos &&
           catalog->state.find("demo::active") != std::string::npos &&
           catalog->state.find("demo::LinkedComponent::internal_state") != std::string::npos ? 0 : 3;
}
