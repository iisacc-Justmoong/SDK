#include "Job.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace ii::job;
namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F &&f) {
    bool rejected = false;
    try { f(); } catch (const std::exception &) { rejected = true; }
    check(rejected, "expected operation to be rejected");
}
std::string item(int n) { return "item-" + std::to_string(n); }
}

int main(int argc, char **argv) try {
    check(argc == 3, "phase and pre-created database required");
    const std::string phase = argv[1];
    Store store(argv[2]);
    if (phase == "first") {
        const auto directory=std::filesystem::path(argv[2]).parent_path();
        rejects([&] { Store missing(directory/"missing.sqlite3"); });
        check(!std::filesystem::exists(directory/"missing.sqlite3"),"store created its own database");
        rejects([&] { Store foreign(directory/"foreign.sqlite3"); });
        { std::ifstream in(directory/"foreign.sqlite3",std::ios::binary);
          const std::string bytes{std::istreambuf_iterator<char>(in),{}};
          check(bytes=="foreign data","foreign database was modified"); }
        JobRequest request{"batch-1", "import", "requester-1", "asset-1", "revision-1", "node-1", true};
        std::vector<ItemRequest> items;
        for (int i = 0; i < 1000; ++i) items.push_back({item(i), "source-" + std::to_string(i), "", "", "label-" + std::to_string(i)});
        store.create(request, items);
        rejects([&] { store.create(request, items); });
        store.begin("batch-1", "node-1", "importing");
        store.progress("batch-1", "copying", 10, 100, "bytes");
        for (int i = 0; i < 1000; ++i) {
            const auto claim = store.claim("batch-1", item(i));
            check(claim.attempt == 1 && !claim.token.empty(), "first attempt was not claimed");
            if (i == 0) rejects([&] { store.succeed("batch-1", item(i), "wrong-token", "bad"); });
            if (i >= 963) {
                store.checkpoint("batch-1", item(i), claim.token, "failed-checkpoint-" + std::to_string(i));
                store.fail("batch-1", item(i), claim.token, "temporary error", true);
            } else store.succeed("batch-1", item(i), claim.token, "artifact-" + std::to_string(i));
        }
        store.finish("batch-1");
        const auto job = store.job("batch-1");
        check(job && job->state == JobState::partial && job->succeeded == 963 && job->failed == 37 &&
              job->total == 1000 && job->requester == "requester-1" && job->baseRevision == "revision-1" &&
              job->processed == 10 && job->totalUnits == 100 && job->unit == "bytes",
              "partial outcome or request metadata was not preserved");
        return 0;
    }
    if (phase == "retry") {
        const auto before = store.job("batch-1");
        check(before && before->state == JobState::partial && before->succeeded == 963 && before->failed == 37,
              "reopening lost partial outcomes");
        check(store.item("batch-1", item(970))->checkpoint == "failed-checkpoint-970", "checkpoint lost after restart");
        check(store.retryFailed("batch-1") == 37, "retry did not select exactly the failed items");
        const auto saved = store.item("batch-1", item(10));
        check(saved && saved->state == ItemState::succeeded && saved->attempts == 1 && saved->artifact == "artifact-10" && saved->label == "label-10",
              "retry changed a successful item");
        store.begin("batch-1", "node-1", "retrying");
        for (int i = 963; i < 999; ++i) {
            const auto claim = store.claim("batch-1", item(i));
            check(claim.attempt == 2, "retry attempt count was not incremented");
            store.succeed("batch-1", item(i), claim.token, "retry-artifact-" + std::to_string(i));
        }
        const auto inFlight = store.claim("batch-1", item(999));
        store.checkpoint("batch-1", item(999), inFlight.token, "published:needs-verification");
        // Exit without marking completion, simulating process loss after a checkpoint.
        return 0;
    }
    if (phase == "recovery") {
        // The host has exclusive ownership of the worker before recovery.
        store.recoverInterrupted("batch-1");
        auto current = store.job("batch-1");
        const auto interrupted = store.item("batch-1", item(999));
        check(current && current->state == JobState::attention && interrupted &&
              interrupted->state == ItemState::unknown && interrupted->checkpoint == "published:needs-verification",
              "interrupted side effect was incorrectly classified or lost");
        check(store.retryFailed("batch-1") == 0, "unknown outcome was automatically retried");
        store.resolveUnknown("batch-1", item(999), false, "verified absent", true);
        check(store.retryFailed("batch-1") == 1, "verified failure was not eligible for retry");
        store.begin("batch-1", "node-1", "retrying verified failure");
        const auto claim = store.claim("batch-1", item(999));
        check(claim.attempt == 3, "attempt history was overwritten");
        store.succeed("batch-1", item(999), claim.token, "final-artifact");
        store.finish("batch-1");
        current = store.job("batch-1");
        check(current && current->state == JobState::succeeded && current->succeeded == 1000 && current->failed == 0,
              "completed retry did not update aggregate counts");
        const auto history=store.history("batch-1", item(999));
        check(history.size() == 3 && history.front().node == "node-1" && history.back().node == "node-1",
              "attempt history or execution node was lost on restart");
        check(store.item("batch-1", item(10))->artifact == "artifact-10", "successful artifact changed");

        store.create({"cancel-1", "import", "requester", "", "", "", true}, {{"one", "a", "", ""}, {"two", "b", "", ""}});
        store.setWaiting("cancel-1", JobState::waitingNode, "connect laptop");
        check(store.job("cancel-1")->reason == "connect laptop", "waiting reason missing");
        store.begin("cancel-1", "laptop", "copying");
        const auto active = store.claim("cancel-1", "one");
        store.requestCancel("cancel-1");
        store.cancelItem("cancel-1", "one", active.token);
        store.finish("cancel-1");
        check(store.job("cancel-1")->state == JobState::cancelled && store.job("cancel-1")->cancelled == 2,
              "cancellation did not preserve per-item states");

        store.create({"cancel-atomic", "import", "requester", "", "", "", true},
                     {{"done", "a", "", ""}, {"published", "b", "", ""}, {"pending", "c", "", ""}});
        store.begin("cancel-atomic", "node", "importing");
        const auto done = store.claim("cancel-atomic", "done");
        store.succeed("cancel-atomic", "done", done.token, "artifact-a");
        const auto saved = store.claim("cancel-atomic", "published");
        store.checkpoint("cancel-atomic", "published", saved.token, "published:artifact-b");
        rejects([&] { store.cancelAndFinish("cancel-atomic"); });
        rejects([&] { store.cancelAndFinish("cancel-atomic", CancelledItem{"published", "wrong-token", ItemState::cancelled}); });
        rejects([&] { store.cancelAndFinish("cancel-atomic", CancelledItem{"published", saved.token, ItemState::succeeded, "artifact-b", "contradictory error"}); });
        check(store.job("cancel-atomic")->state == JobState::running, "failed cancellation changed the job");
        store.cancelAndFinish("cancel-atomic", CancelledItem{"published", saved.token, ItemState::failed, "", "registration interrupted", true});
        const auto cancelledJob = store.job("cancel-atomic");
        check(cancelledJob->state == JobState::cancelled && cancelledJob->succeeded == 1 &&
              cancelledJob->failed == 1 && cancelledJob->cancelled == 1,
              "atomic cancellation lost completed, published or pending items");
        check(store.item("cancel-atomic", "published")->checkpoint == "published:artifact-b" &&
              store.history("cancel-atomic", "published").front().retryable,
              "atomic cancellation discarded the published checkpoint or retry history");
        check(store.retryFailed("cancel-atomic") == 1, "published failure could not be retried");

        store.create({"cancel-pending", "copy", "requester", "", "", "", true}, {{"p", "x", "", ""}});
        store.cancelAndFinish("cancel-pending");
        check(store.job("cancel-pending")->state == JobState::cancelled && store.item("cancel-pending", "p")->state == ItemState::cancelled,
              "pending-only cancellation was not committed");

        store.create({"race-1", "copy", "requester", "", "", "", true}, {{"only", "x", "", ""}});
        check(store.recent(1).front().id == "race-1", "recent jobs are not ordered by creation");
        store.begin("race-1", "node", "copying");
        std::atomic<int> winners = 0;
        auto compete = [&] {
            try { Store peer(argv[2]); (void)peer.claim("race-1", "only"); ++winners; } catch (const std::exception &) {}
        };
        std::thread a(compete), b(compete); a.join(); b.join();
        check(winners == 1, "concurrent workers claimed the same item");
        store.create({"resume-1", "copy", "requester", "", "", "", true},
                     {{"unknown", "source-a", "", ""}, {"pending", "source-b", "", ""}});
        store.begin("resume-1", "node", "copying");
        (void)store.claim("resume-1", "unknown");
        store.recoverInterrupted("resume-1");
        store.resumePending("resume-1"); store.begin("resume-1", "node", "resume pending");
        const auto pending=store.claim("resume-1", "pending");
        store.succeed("resume-1", "pending", pending.token, "result-b"); store.finish("resume-1");
        check(store.job("resume-1")->state==JobState::attention && store.job("resume-1")->unknown==1,
              "unknown result was hidden while resuming unstarted items");
        store.resolveUnknown("resume-1", "unknown", true, "verified-result-a");
        store.finish("resume-1");
        check(store.job("resume-1")->state==JobState::succeeded,
              "verified interrupted result could not finish the job");
        std::cout << "durable batch, retry, checkpoint and cancellation verified\n";
        return 0;
    }
    throw std::runtime_error("unknown phase");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
