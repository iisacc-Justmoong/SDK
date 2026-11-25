#include <iiAgentApplicationInterface>
#include "Application.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using EntryProvider = iiAgentApplicationInterface::NativeProcedureDescriptorEntry;

/// Load a separately built source procedure, run it against a real host object, then unload it.
int main(int argc, char** argv) {
    assert(argc == 2);
#if defined(_WIN32)
    const auto library = LoadLibraryA(argv[1]);
    assert(library);
    const auto provider = reinterpret_cast<EntryProvider>(GetProcAddress(library, iiAgentApplicationInterface::native_procedure_entry_symbol.data()));
#else
    const auto library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(library);
    const auto provider = reinterpret_cast<EntryProvider>(dlsym(library, iiAgentApplicationInterface::native_procedure_entry_symbol.data()));
#endif
    assert(provider);
    const auto descriptor = provider();
    assert(descriptor);
    using namespace iiAgentApplicationInterface;
    Runtime runtime([](const AccessRequest& request) { return request.context.principal == "agent"; });
    auto application = std::make_shared<native_procedure_fixture::Application>();
    assert(runtime.add_object({"application", "Application", "Live application", "Draft"}) == Status::ok);
    assert(runtime.bind_live_object("application", application) == Status::ok);
    assert(runtime.add_capability({"application", "publish-title", "Publish the new title", "text", "text", {}},
        [](std::string_view, std::string&) { return true; },
        [&](const AgentContext&, std::string_view title) {
            return Result{runtime.update_state("application", std::string(title)), std::string(title)};
        }) == Status::ok);
    assert(runtime.execute_procedure({"other"}, *descriptor, "unauthorized").status == Status::denied);
    assert(application->title() == "Draft");
    const auto result = runtime.execute_procedure({"agent"}, *descriptor, "Written from source");
    assert(result.status == Status::ok && result.value == "Written from source");
    assert(application->title() == "Written from source");
    assert(runtime.object({"agent"}, "application")->state == "Written from source");
#if defined(_WIN32)
    assert(FreeLibrary(library));
#else
    assert(dlclose(library) == 0);
#endif
}
