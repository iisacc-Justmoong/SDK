#include <iiAgentApplicationInterface>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <string>
#include <thread>

using namespace iiAgentApplicationInterface;

/// A compiler artifact used to observe authorized external source submissions.
struct TestArtifact final : SourceArtifact {};
/// A loaded procedure invokes the application's existing command under the authenticated subject.
struct TestModule final : LoadedSourceProcedure {
    NativeProcedureDescriptor value{sizeof(NativeProcedureDescriptor), native_procedure_descriptor_version,
        "external.work", +[](ProcedureContext& context, std::string_view input) {
            return context.invoke("doc", "rename", input);
        }};
    const NativeProcedureDescriptor* descriptor() const noexcept override { return &value; }
};
/// The host owns compilation and loading; the gateway does not manufacture a backend.
struct TestBackend final : SourceProcedureBackend {
    int compiles = 0, loads = 0;
    SourceCompilation compile(std::string_view id, std::string_view source) override {
        assert(id == "external.work" && (source == "trusted source" || source.find("draft body") != source.npos));
        ++compiles; return {Status::ok, "", std::make_unique<TestArtifact>()};
    }
    SourceLoad load(const SourceArtifact&) override {
        ++loads; return {Status::ok, "", std::make_unique<TestModule>()};
    }
};

int main() {
    bool approved = true;
    int executions = 0;
    Runtime runtime([&](const AccessRequest& request) {
        if (request.context.principal != "verified-agent" || request.object_id == "hidden") return false;
        return request.action != Action::discover_capability || request.capability_id != "secret";
    });
    assert(runtime.add_object({"a", "document", "first", "private-a"}) == Status::ok);
    assert(runtime.add_object({"doc", "document", "second", "initial"}) == Status::ok);
    assert(runtime.add_object({"hidden", "document", "private", "secret"}) == Status::ok);
    assert(runtime.add_object({"z", "document", "third", "private-z"}) == Status::ok);
    auto validate = [](std::string_view input, std::string&) { return !input.empty(); };
    auto rename = [&](const AgentContext& agent, std::string_view input) {
        assert(agent.principal == "verified-agent");
        ++executions;
        return Result{runtime.update_state("doc", std::string(input)), std::string(input)};
    };
    assert(runtime.add_capability({"doc", "rename", "Rename", "text", "text", {"write"}}, validate, rename) == Status::ok);
    assert(runtime.add_capability({"doc", "secret", "hidden", "", "", {}}, validate, rename) == Status::ok);
    assert(runtime.add_capability({"doc", "z-command", "Other", "", "", {}}, validate, rename) == Status::ok);
    TestBackend backend;
    ExternalGateway gateway(runtime, [&](const ExternalPeer& peer, std::string_view token) -> std::optional<AgentContext> {
        if (!approved || peer.user_id != 7 || token != "verified-secret") return std::nullopt;
        return AgentContext{"verified-agent"};
    }, &backend);
    ExternalPeer peer; peer.user_id = 7;
    ExternalRequest request; request.operation = ExternalOperation::list_objects; request.limit = 1;
    assert(gateway.handle(peer, request).status == Status::denied);
    request.credential = "verified-secret";
    request.operation = ExternalOperation::describe;
    Status wrong_thread = Status::ok;
    std::thread outsider([&] { wrong_thread = gateway.handle(peer, request).status; });
    outsider.join();
    assert(wrong_thread == Status::invalid_argument);
    const auto info = gateway.handle(peer, request);
    assert(info.status == Status::ok && info.operations.size() == 7);
    assert(info.operations[1].name == "list_objects" && info.operations[1].optional.size() == 2);
    assert(info.operations[2].description.find("host-published") != std::string::npos);
    assert(info.operations.back().description.find("write_object<T>") != std::string::npos);
    request.operation = ExternalOperation::list_objects;
    auto page = gateway.handle(peer, request);
    assert(page.status == Status::ok && page.objects.size() == 1 && page.objects[0].id == "a" && page.next_cursor == "a");
    request.cursor = page.next_cursor;
    page = gateway.handle(peer, request);
    assert(page.status == Status::ok && page.objects.size() == 1 && page.objects[0].id == "doc" && page.next_cursor == "doc");
    request.cursor = page.next_cursor;
    page = gateway.handle(peer, request);
    assert(page.status == Status::ok && page.objects.size() == 1 && page.objects[0].id == "z" && page.next_cursor.empty());
    request.limit = 0;
    assert(gateway.handle(peer, request).status == Status::invalid_argument);
    request.operation = ExternalOperation::list_capabilities; request.object_id.clear(); request.cursor.clear(); request.limit = 1;
    assert(gateway.handle(peer, request).status == Status::invalid_argument);
    request.operation = ExternalOperation::invoke; request.object_id = "doc"; request.capability_id.clear();
    assert(gateway.handle(peer, request).status == Status::invalid_argument);
    request.operation = ExternalOperation::read_object; request.object_id = "hidden";
    assert(gateway.handle(peer, request).status == Status::denied);
    request.object_id = "doc";
    auto object = gateway.handle(peer, request);
    assert(object.status == Status::ok && object.object && object.object->state == "initial");
    request.operation = ExternalOperation::list_capabilities; request.cursor.clear(); request.limit = 1;
    auto capabilities = gateway.handle(peer, request);
    assert(capabilities.status == Status::ok && capabilities.capabilities.size() == 1 && capabilities.capabilities[0].id == "rename");
    assert(capabilities.next_cursor == "rename");
    request.cursor = capabilities.next_cursor;
    capabilities = gateway.handle(peer, request);
    assert(capabilities.capabilities.size() == 1 && capabilities.capabilities[0].id == "z-command" && capabilities.next_cursor.empty());
    request.operation = ExternalOperation::invoke; request.capability_id = "rename"; request.input = "external change";
    assert(gateway.handle(peer, request).status == Status::ok && runtime.object({"verified-agent"}, "doc")->state == "external change");
    request.operation = ExternalOperation::execute_source; request.procedure_id = "external.work"; request.source = "trusted source";
    request.input = "source change";
    assert(gateway.handle(peer, request).status == Status::ok && backend.compiles == 1 && backend.loads == 1);
    assert(runtime.object({"verified-agent"}, "doc")->state == "source change" && executions == 2);
    request.operation = ExternalOperation::execute_draft; request.body = "// draft body\nreturn {};"; request.input = "draft change";
    assert(gateway.handle(peer, request).status == Status::ok && backend.compiles == 2 && backend.loads == 2);
    assert(runtime.object({"verified-agent"}, "doc")->state == "draft change" && executions == 3);
    approved = false;
    assert(gateway.handle(peer, request).status == Status::denied && backend.compiles == 2);
    ExternalGateway no_source(runtime, [](const ExternalPeer&, std::string_view) -> std::optional<AgentContext> {
        return AgentContext{"verified-agent"};
    });
    request.operation = ExternalOperation::describe;
    assert(no_source.handle(peer, request).operations.size() == 5);
    request.operation = ExternalOperation::execute_source;
    assert(no_source.handle(peer, request).status == Status::handler_failed);
    ExternalGateway no_auth(runtime, {});
    assert(no_auth.handle(peer, request).status == Status::denied);
    ExternalGateway throwing(runtime, [](const ExternalPeer&, std::string_view) -> std::optional<AgentContext> { throw 1; });
    assert(throwing.handle(peer, request).status == Status::denied);
}
