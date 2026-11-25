#include <iiAgentApplicationInterface>

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

using namespace iiAgentApplicationInterface;

int main() {
    Runtime denied;
    assert(denied.add_object({"doc", "document", "Example", "old"}) == Status::ok);
    assert(denied.objects({"agent"}).empty());
    assert(!denied.object({"agent"}, "doc"));
    const auto missing = denied.invoke({"agent"}, "doc", "rename", "new");
    assert(missing.status == Status::denied && missing.value == "command unavailable");

    bool enabled = true;
    Runtime runtime([&](const AccessRequest& request) {
        return enabled && request.context.principal == "agent" && request.object_id != "hidden";
    });
    assert(runtime.add_object({"doc", "document", "Example", "old"}) == Status::ok);
    assert(runtime.add_object({"hidden", "document", "Secret", "classified"}) == Status::ok);
    assert(runtime.add_object({"doc", "document", "Duplicate", ""}) == Status::already_exists);
    assert(runtime.add_object({"", "document", "Invalid", ""}) == Status::invalid_argument);
    assert(runtime.objects({"agent"}).size() == 1);
    auto first = runtime.object({"agent"}, "doc");
    assert(first && first->state == "old" && first->revision == 1 && first->instance != 0);
    const auto original_instance = first->instance;
    int calls = 0;
    Capability rename{"doc", "rename", "Rename document", "input schema", "output schema", {"document.write"}};
    assert(runtime.add_capability(rename,
        [](std::string_view input, std::string& reason) { reason = "empty"; return !input.empty(); },
        [&](const AgentContext&, std::string_view input) {
            ++calls;
            assert(runtime.update_state("doc", std::string(input)) == Status::ok);
            return Result{Status::ok, "done"};
        }) == Status::ok);
    assert(runtime.add_capability(rename, {}, {}) == Status::invalid_argument);
    assert(runtime.capabilities({"agent"}, "doc").size() == 1);
    assert(runtime.capabilities({"agent"}, "hidden").empty());
    std::vector<Event> events;
    const auto subscription = runtime.subscribe({"agent"}, [&](const Event& e) { events.push_back(e); });
    assert(subscription != 0);
    auto invalid = runtime.invoke({"agent"}, "doc", "rename", "");
    assert(invalid.status == Status::validation_failed && calls == 0);
    enabled = false;
    const auto hidden_command = runtime.invoke({"agent"}, "doc", "rename", "new");
    assert(hidden_command.status == Status::denied && hidden_command.value == "command unavailable");
    assert(calls == 0);
    assert(runtime.update_state("doc", "quiet") == Status::ok && events.empty());
    enabled = true;
    assert(runtime.invoke({"agent"}, "doc", "rename", "new").status == Status::ok);
    assert(calls == 1);
    assert(events.size() == 2 && events[0].kind == EventKind::state_changed && events[1].kind == EventKind::command_completed);
    assert(events[0].instance == original_instance && events[0].revision == 3);
    assert(runtime.object({"agent"}, "doc")->state == "new");
    assert(runtime.update_state("doc", "new") == Status::ok && events.size() == 2);
    runtime.unsubscribe(subscription);
    assert(runtime.update_state("doc", "again") == Status::ok && events.size() == 2);
    assert(runtime.remove_object("doc") == Status::ok);
    assert(runtime.add_object({"doc", "document", "New instance", ""}) == Status::ok);
    assert(runtime.object({"agent"}, "doc")->instance != original_instance);
    assert(runtime.capabilities({"agent"}, "doc").empty());

    // UI and agent identities reach the same application-owned command handler.
    Runtime shared([](const AccessRequest& request) {
        return request.context.principal == "ui" || request.context.principal == "agent";
    });
    assert(shared.add_object({"shared", "document", "", "draft"}) == Status::ok);
    std::vector<std::string> actors;
    assert(shared.add_capability({"shared", "rename", "", "", "", {"write"}},
        [](std::string_view input, std::string&) { return !input.empty(); },
        [&](const AgentContext& context, std::string_view input) {
            actors.push_back(context.principal);
            return Result{shared.update_state("shared", std::string(input)), ""};
        }) == Status::ok);
    assert(shared.invoke({"ui"}, "shared", "rename", "UI edit").status == Status::ok);
    assert(shared.invoke({"agent"}, "shared", "rename", "Agent edit").status == Status::ok);
    assert((actors == std::vector<std::string>{"ui", "agent"}));
    assert(shared.object({"ui"}, "shared")->state == "Agent edit");

    // A throwing policy fails closed for both discovery and dispatch.
    Runtime throwing([](const AccessRequest&) -> bool { throw std::runtime_error("policy"); });
    assert(throwing.add_object({"x", "type", "", ""}) == Status::ok);
    assert(throwing.objects({"agent"}).empty());

    // A failing validator never reaches the handler; a failing handler is reported.
    Runtime failures([](const AccessRequest&) { return true; });
    assert(failures.add_object({"f", "type", "", ""}) == Status::ok);
    int failed_handler_calls = 0;
    assert(failures.add_capability({"f", "invalid", "", "", "", {}},
        [](std::string_view, std::string&) -> bool { throw std::runtime_error("validator"); },
        [&](const AgentContext&, std::string_view) { ++failed_handler_calls; return Result{}; }) == Status::ok);
    assert(failures.invoke({"a"}, "f", "invalid", "").status == Status::validation_failed);
    assert(failed_handler_calls == 0);
    assert(failures.add_capability({"f", "throws", "", "", "", {}},
        [](std::string_view, std::string&) { return true; },
        [](const AgentContext&, std::string_view) -> Result { throw std::runtime_error("handler"); }) == Status::ok);
    std::vector<EventKind> failed_events;
    failures.subscribe({"a"}, [&](const Event& event) { failed_events.push_back(event.kind); });
    assert(failures.invoke({"a"}, "f", "throws", "").status == Status::handler_failed);
    assert((failed_events == std::vector<EventKind>{EventKind::command_failed}));

    // Even a host validator violating the no-reentry rule cannot execute a replaced command.
    Runtime replaced([](const AccessRequest&) { return true; });
    assert(replaced.add_object({"r", "type", "", ""}) == Status::ok);
    int replaced_calls = 0;
    Capability replacement{"r", "edit", "", "", "", {}};
    assert(replaced.add_capability(replacement,
        [&](std::string_view, std::string&) {
            assert(replaced.remove_capability("r", "edit") == Status::ok);
            assert(replaced.add_capability(replacement,
                [](std::string_view, std::string&) { return true; },
                [&](const AgentContext&, std::string_view) { ++replaced_calls; return Result{}; }) == Status::ok);
            return true;
        },
        [&](const AgentContext&, std::string_view) { ++replaced_calls; return Result{}; }) == Status::ok);
    assert(replaced.invoke({"a"}, "r", "edit", "").status == Status::not_found);
    assert(replaced_calls == 0);

    // Observer failures cannot abort committed state or prevent other observers.
    Runtime observers([](const AccessRequest&) { return true; });
    assert(observers.add_object({"x", "type", "", ""}) == Status::ok);
    std::vector<std::string> order;
    observers.subscribe({"a"}, [&](const Event& e) {
        if (e.kind == EventKind::state_changed && e.revision == 2) {
            order.push_back("a2");
            assert(observers.update_state("x", "nested") == Status::ok);
        } else if (e.kind == EventKind::state_changed) order.push_back("a3");
        throw std::runtime_error("observer");
    });
    observers.subscribe({"b"}, [&](const Event& e) { if (e.kind == EventKind::state_changed) order.push_back("b" + std::to_string(e.revision)); });
    assert(observers.update_state("x", "outer") == Status::ok);
    assert((order == std::vector<std::string>{"a2", "b2", "a3", "b3"}));
    assert(observers.object({"a"}, "x")->state == "nested");
    // All events caused by one object removal precede a reentrant re-registration.
    Runtime reentrant([](const AccessRequest&) { return true; });
    assert(reentrant.add_object({"x", "type", "", ""}) == Status::ok);
    assert(reentrant.add_capability({"x", "run", "", "", "", {}},
        [](std::string_view, std::string&) { return true; },
        [](const AgentContext&, std::string_view) { return Result{}; }) == Status::ok);
    std::vector<EventKind> removal_order;
    reentrant.subscribe({"a"}, [&](const Event& event) {
        removal_order.push_back(event.kind);
        if (event.kind == EventKind::capability_removed)
            assert(reentrant.add_object({"x", "type", "replacement", ""}) == Status::ok);
    });
    assert(reentrant.remove_object("x") == Status::ok);
    assert((removal_order == std::vector<EventKind>{EventKind::capability_removed,
            EventKind::object_removed, EventKind::object_added}));

    // A subscriber cannot receive a change published before it subscribed.
    Runtime late([](const AccessRequest&) { return true; });
    assert(late.add_object({"x", "type", "", ""}) == Status::ok);
    std::vector<std::uint64_t> received_late;
    late.subscribe({"first"}, [&](const Event& event) {
        if (event.kind == EventKind::state_changed && event.revision == 2) {
            assert(late.update_state("x", "third") == Status::ok);
            late.subscribe({"late"}, [&](const Event& e) { received_late.push_back(e.revision); });
        }
    });
    assert(late.update_state("x", "second") == Status::ok);
    assert(received_late.empty());
    assert(late.update_state("x", "fourth") == Status::ok);
    assert((received_late == std::vector<std::uint64_t>{4}));
    return 0;
}
