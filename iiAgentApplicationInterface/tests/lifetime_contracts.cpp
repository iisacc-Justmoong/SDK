#include <iiAgentApplicationInterface>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

using namespace iiAgentApplicationInterface;

/// An application object whose destructor can be observed independently of registry events.
struct Document : std::enable_shared_from_this<Document> {
    explicit Document(int& destroyed, std::string value) : destroyed_(&destroyed), name(std::move(value)) {}
    ~Document() { ++*destroyed_; }
    int* destroyed_;
    std::string name;
};

int main() {
    int destroyed = 0;
    std::vector<Event> events;
    Runtime runtime([](const AccessRequest&) { return true; });
    runtime.subscribe({"agent"}, [&](const Event& event) { events.push_back(event); });
    auto tracked = runtime.make_tracked_object<Document>({"", "Document", "", ""}, destroyed, "Draft");
    assert(tracked.status == Status::ok && tracked.object && !tracked.object_id.empty());
    assert(events.size() == 1 && events[0].kind == EventKind::object_added);
    assert(runtime.object({"agent"}, tracked.object_id));
    assert(tracked.object->shared_from_this() == tracked.object);
    std::shared_ptr<const Document> pinned;
    assert(runtime.execute_procedure({"agent"}, "inspect", [&](ProcedureContext& context, std::string_view) {
        pinned = context.read_object<Document>(tracked.object_id);
        return Result{pinned && pinned->name == "Draft" ? Status::ok : Status::handler_failed, ""};
    }, "").status == Status::ok);
    tracked.object.reset();
    assert(destroyed == 0 && runtime.object({"agent"}, tracked.object_id));
    pinned.reset();
    assert(destroyed == 1 && !runtime.object({"agent"}, tracked.object_id));
    assert(events.back().kind == EventKind::object_removed);

    auto previous = runtime.make_tracked_object<Document>({"reused", "Document", "", ""}, destroyed, "old");
    assert(previous.status == Status::ok);
    assert(runtime.remove_object("reused") == Status::ok);
    assert(runtime.add_object({"reused", "Document", "replacement", ""}) == Status::ok);
    previous.object.reset();
    assert(runtime.object({"agent"}, "reused")->description == "replacement");

    std::shared_ptr<Document> survives_runtime;
    {
        Runtime shorter([](const AccessRequest&) { return true; });
        auto value = shorter.make_tracked_object<Document>({"owned", "Document", "", ""}, destroyed, "long");
        assert(value.status == Status::ok);
        survives_runtime = std::move(value.object);
    }
    survives_runtime.reset();
    assert(destroyed == 3);

    // A worker's last release is marshalled to the application's owner thread.
    std::vector<std::function<void()>> dispatched;
    std::mutex dispatch_mutex;
    assert(runtime.set_lifecycle_dispatcher([&](std::function<void()> action) {
        std::lock_guard lock(dispatch_mutex);
        dispatched.push_back(std::move(action));
    }) == Status::ok);
    auto worker_value = runtime.make_tracked_object<Document>({"worker", "Document", "", ""}, destroyed, "worker");
    std::thread worker([object = std::move(worker_value.object)]() mutable { object.reset(); });
    worker.join();
    assert(runtime.object({"agent"}, "worker"));
    assert(dispatched.size() == 1);
    dispatched.front()();
    dispatched.clear();
    assert(!runtime.object({"agent"}, "worker"));
    assert(events.back().kind == EventKind::object_removed);
    assert(destroyed == 4);

    // With no dispatcher, the owner can explicitly drain safely queued retirements.
    assert(runtime.set_lifecycle_dispatcher({}) == Status::ok);
    auto delayed = runtime.make_tracked_object<Document>({"delayed", "Document", "", ""}, destroyed, "delayed");
    std::thread delayed_worker([object = std::move(delayed.object)]() mutable { object.reset(); });
    delayed_worker.join();
    assert(runtime.object({"agent"}, "delayed"));
    assert(runtime.drain_lifecycle() == Status::ok);
    assert(!runtime.object({"agent"}, "delayed"));
    assert(destroyed == 5);

    // Incorrect dispatch onto a worker leaves the retirement queued for its owner.
    assert(runtime.set_lifecycle_dispatcher([](std::function<void()> action) { action(); }) == Status::ok);
    auto wrong_thread = runtime.make_tracked_object<Document>({"wrong", "Document", "", ""}, destroyed, "wrong");
    std::thread wrong_worker([object = std::move(wrong_thread.object)]() mutable { object.reset(); });
    wrong_worker.join();
    assert(runtime.object({"agent"}, "wrong"));
    assert(runtime.drain_lifecycle() == Status::ok);
    assert(!runtime.object({"agent"}, "wrong"));
    assert(destroyed == 6);

    // A posted callback is harmless even if the runtime dies before it runs.
    std::function<void()> late_callback;
    {
        Runtime shorter([](const AccessRequest&) { return true; });
        assert(shorter.set_lifecycle_dispatcher([&](std::function<void()> action) {
            late_callback = std::move(action);
        }) == Status::ok);
        auto value = shorter.make_tracked_object<Document>({"late", "Document", "", ""}, destroyed, "late");
        std::thread late_worker([object = std::move(value.object)]() mutable { object.reset(); });
        late_worker.join();
        assert(late_callback);
    }
    late_callback();
    assert(destroyed == 7);
}
