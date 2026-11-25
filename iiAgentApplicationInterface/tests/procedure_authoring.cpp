#include <iiAgentApplicationInterface>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>

using namespace iiAgentApplicationInterface;

/// A live application object used to check native-authoring access decisions.
struct Document { std::string title; };

/// An owned artifact records both source generation and destruction.
struct Artifact final : SourceArtifact {
    Artifact(std::string identifier, int& released) : id(std::move(identifier)), releases(&released) {}
    ~Artifact() override { ++*releases; }
    std::string id;
    int* releases;
};

/// A loaded module invokes the same application command as a generated procedure.
struct Module final : LoadedSourceProcedure {
    explicit Module(std::string identifier) : id(std::move(identifier)), descriptor_value{
        sizeof(NativeProcedureDescriptor), native_procedure_descriptor_version, id.c_str(),
        +[](ProcedureContext& context, std::string_view input) { return context.invoke("doc", "rename", input); }} {}
    const NativeProcedureDescriptor* descriptor() const noexcept override { return &descriptor_value; }
    std::string id;
    NativeProcedureDescriptor descriptor_value;
};

/// Records build and load activity without depending on an external compiler.
struct Backend final : SourceProcedureBackend {
    int compiles = 0, loads = 0, releases = 0;
    std::string captured_source;
    SourceCompilation compile(std::string_view identifier, std::string_view source) override {
        ++compiles;
        captured_source = source;
        return {Status::ok, "", std::make_unique<Artifact>(std::string(identifier), releases)};
    }
    SourceLoad load(const SourceArtifact& source) override {
        ++loads;
        auto* artifact = dynamic_cast<const Artifact*>(&source);
        if (!artifact) return {Status::invalid_argument, "wrong backend", {}};
        return {Status::ok, "", std::make_unique<Module>(artifact->id)};
    }
};

int main() {
    bool compile = false, load = false, execute = false, native_read = true, native_write = false;
    Runtime runtime([&](const AccessRequest& request) {
        if (request.context.principal != "agent" || request.object_id == "hidden") return false;
        if (request.action == Action::discover_capability && request.capability_id == "remove") return false;
        if (request.action == Action::read_native_object) return native_read;
        if (request.action == Action::write_native_object) return native_write;
        if (request.action == Action::compile_procedure) return compile;
        if (request.action == Action::load_procedure) return load;
        if (request.action == Action::execute_procedure) return execute;
        return true;
    });
    auto document = std::make_shared<Document>();
    assert(runtime.add_object({"doc", "Document", "a note", "old"}) == Status::ok);
    assert(runtime.bind_live_object("doc", document) == Status::ok);
    assert(runtime.add_object({"hidden", "Document", "private", ""}) == Status::ok);
    assert(runtime.add_object({"application.build_catalog", "catalog", "", "{\"declarations\":[]}"}) == Status::ok);
    auto validator = [](std::string_view input, std::string&) { return !input.empty(); };
    auto rename = [&](const AgentContext&, std::string_view input) {
        document->title = input;
        return Result{runtime.update_state("doc", document->title), document->title};
    };
    assert(runtime.add_capability({"doc", "rename", "Rename document", "text", "text", {"write"}}, validator, rename) == Status::ok);
    assert(runtime.add_capability({"doc", "remove", "Hidden command", "", "", {}}, validator, rename) == Status::ok);

    auto snapshot = runtime.procedure_authoring_context({"agent"});
    assert(snapshot.language == "C++23" && snapshot.entry_symbol == native_procedure_entry_symbol);
    assert(std::any_of(snapshot.context_api.begin(), snapshot.context_api.end(), [](const auto& method) {
        return method.name == "invoke" && method.signature.find("std::string_view") != std::string_view::npos;
    }));
    assert(snapshot.objects.size() == 2);
    auto doc = std::find_if(snapshot.objects.begin(), snapshot.objects.end(), [](const auto& item) { return item.object.id == "doc"; });
    assert(doc != snapshot.objects.end() && doc->object.state == "old");
    assert(doc->capabilities.size() == 1 && doc->capabilities[0].id == "rename");
    assert(doc->native_readable && !doc->native_writable);
    assert(runtime.procedure_authoring_context({"stranger"}).objects.empty());
    native_read = false;
    assert(!runtime.procedure_authoring_context({"agent"}).objects.back().native_readable);

    const std::string id = "draft.\"\\\n한글";
    const ProcedureDraft draft{id, "#include <string>", "return context.invoke(\"doc\", \"rename\", input);"};
    const auto assembled = compose_procedure_source(draft);
    assert(assembled.status == Status::ok && assembled.value.find("iiagent_application_procedure_v1") != std::string::npos);
    assert(assembled.value.find(draft.body) != std::string::npos);
    assert(assembled.value.find(id) == std::string::npos); // hostile identifiers are not inserted verbatim.
    assert(compose_procedure_source({"", "", "return {};"}).status == Status::invalid_argument);
    assert(compose_procedure_source({"draft", "", ""}).status == Status::invalid_argument);
    assert(compose_procedure_source({std::string("x\0y", 3), "", "return {};"}).status == Status::invalid_argument);

    Backend backend, other;
    auto denied = runtime.build_procedure({"agent"}, draft, backend);
    assert(denied.status == Status::denied && !denied.procedure && backend.compiles == 0);
    compile = true;
    auto built = runtime.build_procedure({"agent"}, draft, backend);
    assert(built.status == Status::ok && built.procedure && built.procedure->identifier() == id);
    assert(backend.compiles == 1 && backend.captured_source == assembled.value && backend.releases == 0);
    assert(runtime.execute_built({"agent"}, *built.procedure, other, "new").status == Status::invalid_argument);
    assert(runtime.execute_built({"agent"}, *built.procedure, backend, "new").status == Status::denied && backend.loads == 0);
    load = true;
    assert(runtime.execute_built({"agent"}, *built.procedure, backend, "new").status == Status::denied && backend.loads == 1);
    execute = true;
    compile = false; // Reuse needs fresh load/execute permission, not a second compilation.
    assert(runtime.execute_built({"agent"}, *built.procedure, backend, "first").value == "first");
    assert(runtime.execute_built({"agent"}, *built.procedure, backend, "second").value == "second");
    assert(document->title == "second" && backend.compiles == 1 && backend.loads == 3);
    assert(runtime.object({"agent"}, "doc")->state == "second");
    built.procedure.reset();
    assert(backend.releases == 1);
}
