#include <iiAgentApplicationInterface>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <string>

using namespace iiAgentApplicationInterface;

/// A backend-owned compiler artifact whose destruction is observable.
struct Artifact final : SourceArtifact {
    explicit Artifact(int& releases) : releases_(&releases) {}
    ~Artifact() override { ++*releases_; }
    int* releases_;
};
/// An in-process loaded module whose lifetime covers the procedure call.
struct Module final : LoadedSourceProcedure {
    Module(int& releases, std::string identifier) : releases_(&releases), name_(std::move(identifier)) {
        descriptor_ = {sizeof(NativeProcedureDescriptor), native_procedure_descriptor_version,
                       name_.c_str(), +[](ProcedureContext& context, std::string_view text) {
                           return Result{Status::ok, context.subject().principal + ":" + std::string(text)};
                       }};
    }
    ~Module() override { ++*releases_; }
    const NativeProcedureDescriptor* descriptor() const noexcept override { return &descriptor_; }
    int* releases_;
    std::string name_;
    NativeProcedureDescriptor descriptor_{};
};
/// A deterministic test backend records policy ordering and resource lifetimes.
struct Backend final : SourceProcedureBackend {
    int compilations = 0, loads = 0, artifacts_released = 0, modules_released = 0;
    std::string loaded_id = "work";
    SourceCompilation compile(std::string_view id, std::string_view source) override {
        assert(id == "work" && source == "trusted source");
        ++compilations;
        return {Status::ok, "", std::make_unique<Artifact>(artifacts_released)};
    }
    SourceLoad load(const SourceArtifact&) override {
        ++loads;
        return {Status::ok, "", std::make_unique<Module>(modules_released, loaded_id)};
    }
};

int main() {
    bool compile_allowed = false, load_allowed = false, execute_allowed = false;
    Runtime runtime([&](const AccessRequest& request) {
        if (request.context.principal != "agent") return false;
        if (request.action == Action::compile_procedure) return compile_allowed;
        if (request.action == Action::load_procedure) return load_allowed;
        if (request.action == Action::execute_procedure) return execute_allowed;
        return true;
    });
    Backend backend;
    assert(runtime.execute_source({"agent"}, "work", "trusted source", backend, "input").status == Status::denied);
    assert(backend.compilations == 0 && backend.loads == 0);
    compile_allowed = true;
    assert(runtime.execute_source({"agent"}, "work", "trusted source", backend, "input").status == Status::denied);
    assert(backend.compilations == 1 && backend.loads == 0 && backend.artifacts_released == 1);
    load_allowed = true;
    backend.loaded_id = "other";
    assert(runtime.execute_source({"agent"}, "work", "trusted source", backend, "input").status == Status::invalid_argument);
    assert(backend.loads == 1 && backend.modules_released == 1 && backend.artifacts_released == 2);
    backend.loaded_id = "work";
    assert(runtime.execute_source({"agent"}, "work", "trusted source", backend, "input").status == Status::denied);
    assert(backend.modules_released == 2 && backend.artifacts_released == 3);
    execute_allowed = true;
    const auto result = runtime.execute_source({"agent"}, "work", "trusted source", backend, "input");
    assert(result.status == Status::ok && result.value == "agent:input");
    assert(backend.compilations == 4 && backend.loads == 3 && backend.modules_released == 3 && backend.artifacts_released == 4);
}
