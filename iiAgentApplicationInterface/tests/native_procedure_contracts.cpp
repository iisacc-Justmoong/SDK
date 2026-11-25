#include <iiAgentApplicationInterface>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <string>
#include <vector>

using namespace iiAgentApplicationInterface;

/// A host-owned application object whose real API a procedure may call.
struct Document {
    std::string name;
    void rename(std::string value) { name = std::move(value); }
};

int main() {
    bool may_write = false;
    Runtime runtime([&](const AccessRequest& request) {
        if (request.context.principal != "agent") return false;
        if (request.action == Action::write_native_object) return may_write;
        return true;
    });
    auto document = std::make_shared<Document>();
    document->name = "Draft";
    assert(runtime.add_object({"doc", "Document", "Current document", "Draft"}) == Status::ok);
    assert(runtime.bind_live_object("doc", document) == Status::ok);
    assert(runtime.bind_live_object("doc", document) == Status::already_exists);
    std::vector<EventKind> procedure_events;
    std::string last_procedure_id;
    runtime.subscribe({"agent"}, [&](const Event& event) {
        if (event.kind == EventKind::procedure_completed || event.kind == EventKind::procedure_failed) {
            procedure_events.push_back(event.kind);
            last_procedure_id = event.capability_id;
        }
    });
    int called = 0;
    auto procedure = [&](ProcedureContext& app, std::string_view name) {
        ++called;
        const auto read = app.read_object<Document>("doc");
        assert(read && read->name == "Draft");
        assert(!app.read_object<std::string>("doc"));
        const auto write = app.write_object<Document>("doc");
        if (!write) return Result{Status::denied, "write unavailable"};
        write->rename(std::string(name));
        return Result{Status::ok, write->name};
    };
    NativeProcedureDescriptor unsupported{sizeof(NativeProcedureDescriptor),
        native_procedure_descriptor_version + 1, "future",
        +[](ProcedureContext&, std::string_view) { return Result{}; }};
    assert(runtime.execute_procedure({"agent"}, unsupported, "").status == Status::invalid_argument);
    unsupported.version = native_procedure_descriptor_version;
    unsupported.size = 0;
    assert(runtime.execute_procedure({"agent"}, unsupported, "").status == Status::invalid_argument);
    assert(runtime.execute_procedure({"other"}, "rename", procedure, "A").status == Status::denied);
    assert(called == 0 && document->name == "Draft" && procedure_events.empty());
    assert(runtime.execute_procedure({"agent"}, "rename", procedure, "A").status == Status::denied);
    assert(called == 1 && document->name == "Draft");
    may_write = true;
    assert(runtime.execute_procedure({"agent"}, "rename", procedure, "A").status == Status::ok);
    assert(called == 2 && document->name == "A");
    assert((procedure_events == std::vector<EventKind>{EventKind::procedure_failed, EventKind::procedure_completed}));
    std::string procedure_id = "stable-id";
    assert(runtime.execute_procedure({"agent"}, procedure_id, [&](ProcedureContext&, std::string_view) {
        procedure_id.clear();
        return Result{};
    }, "").status == Status::ok);
    assert(last_procedure_id == "stable-id");
    assert(runtime.add_capability({"doc", "rename", "Rename document", "text", "text", {"write"}},
        [](std::string_view input, std::string&) { return !input.empty(); },
        [&](const AgentContext&, std::string_view value) {
            document->rename(std::string(value));
            return Result{runtime.update_state("doc", document->name), document->name};
        }) == Status::ok);
    assert(runtime.execute_procedure({"agent"}, "through-command", [](ProcedureContext& app, std::string_view) {
        return app.invoke("doc", "rename", "B");
    }, "").status == Status::ok);
    assert(document->name == "B" && runtime.object({"agent"}, "doc")->state == "B");
    assert(runtime.execute_procedure({"agent"}, "failure", [](ProcedureContext&, std::string_view) -> Result {
        throw 1;
    }, "").status == Status::handler_failed);
    assert(runtime.unbind_live_object("doc") == Status::ok);
    assert(runtime.execute_procedure({"agent"}, "unbound", [](ProcedureContext& app, std::string_view) {
        assert(!app.read_object<Document>("doc"));
        return Result{};
    }, "").status == Status::ok);
    assert(runtime.bind_live_object("doc", document) == Status::ok);
    assert(runtime.remove_object("doc") == Status::ok);
    assert(runtime.add_object({"doc", "Document", "Replacement", ""}) == Status::ok);
    assert(runtime.execute_procedure({"agent"}, "stale", [](ProcedureContext& app, std::string_view) {
        assert(!app.read_object<Document>("doc"));
        return Result{};
    }, "").status == Status::ok);
    assert(runtime.bind_live_object("doc", document) == Status::ok);
    document.reset();
    assert(runtime.execute_procedure({"agent"}, "expired", [](ProcedureContext& app, std::string_view) {
        assert(!app.read_object<Document>("doc"));
        return Result{};
    }, "").status == Status::ok);
}
