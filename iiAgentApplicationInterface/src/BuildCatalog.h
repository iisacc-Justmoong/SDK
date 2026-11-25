#pragma once

#include "Agent.h"

namespace iiAgentApplicationInterface::generated {

/// Publishes the build-generated source declaration catalog into this runtime.
/// The application target must enable build discovery to define this function.
/// Discovery describes source declarations; it does not bind live object instances.
Status publish_build_catalog(Runtime& runtime);

/// Creates a runtime and automatically publishes this application's build catalog.
/// Returns an empty pointer if the generated catalog cannot be registered.
std::unique_ptr<Runtime> create_application_runtime(Authorizer authorizer = {});

} // namespace iiAgentApplicationInterface::generated
