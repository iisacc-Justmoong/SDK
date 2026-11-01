#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// C++23, Qt-free contracts. The host owns file creation and execution; this
// module only opens an existing database and persists explicit job outcomes.
namespace ii::job {

enum class JobState { waiting, waitingNode, waitingPermission, running, partial, failed,
                      cancelRequested, cancelled, succeeded, attention };
enum class ItemState { pending, running, succeeded, failed, cancelled, unknown, skipped };

struct JobRequest {
    std::string id, kind, requester, resource, baseRevision, targetNode;
    bool cancellable = true;
};
struct ItemRequest {
    std::string id, source, resource, baseRevision;
    std::string label;
};
struct Job {
    std::string id, kind, requester, resource, baseRevision, targetNode, actualNode, stage, reason, unit;
    JobState state = JobState::waiting;
    std::uint64_t total = 0, pending = 0, running = 0, succeeded = 0, failed = 0,
                  cancelled = 0, unknown = 0, skipped = 0, processed = 0;
    std::optional<std::uint64_t> totalUnits;
    std::uint64_t retryCount = 0;
    bool cancellable = true;
};
struct Item {
    std::string id, source, resource, baseRevision, checkpoint, artifact, error;
    std::string label;
    ItemState state = ItemState::pending;
    std::uint64_t attempts = 0;
    bool retryable = false;
};
struct Attempt {
    std::uint64_t number = 0;
    ItemState state = ItemState::pending;
    std::string checkpoint, artifact, error, node;
    bool retryable = false;
};
struct Claim { std::string token, previousCheckpoint; std::uint64_t attempt = 0; };
struct CancelledItem {
    std::string id, token;
    ItemState state = ItemState::cancelled;
    std::string artifact, error;
    bool retryable = false;
};

class Store final {
public:
    // The host must pre-create a regular database file through its file owner.
    // An empty file is initialized; a nonempty foreign or newer DB is rejected.
    explicit Store(const std::filesystem::path &database);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    void create(const JobRequest &, const std::vector<ItemRequest> &);
    [[nodiscard]] std::optional<Job> job(const std::string &id) const;
    [[nodiscard]] std::vector<Job> jobs(const std::string &afterId = {}, std::size_t limit = 100) const;
    // Insertion order, most recent first; intended for a bounded history view.
    [[nodiscard]] std::vector<Job> recent(std::size_t limit = 100) const;
    [[nodiscard]] std::optional<Item> item(const std::string &jobId, const std::string &itemId) const;
    [[nodiscard]] std::vector<Item> items(const std::string &jobId, const std::string &afterId = {}, std::size_t limit = 100) const;
    [[nodiscard]] std::vector<Attempt> history(const std::string &jobId, const std::string &itemId) const;

    void setWaiting(const std::string &jobId, JobState waitingState, const std::string &reason);
    void begin(const std::string &jobId, const std::string &node, const std::string &stage);
    void progress(const std::string &jobId, const std::string &stage, std::uint64_t processed,
                  std::optional<std::uint64_t> total, const std::string &unit);
    [[nodiscard]] Claim claim(const std::string &jobId, const std::string &itemId);
    void checkpoint(const std::string &jobId, const std::string &itemId, const std::string &token, const std::string &value);
    void succeed(const std::string &jobId, const std::string &itemId, const std::string &token, const std::string &artifact);
    void fail(const std::string &jobId, const std::string &itemId, const std::string &token, const std::string &error, bool retryable);
    void skip(const std::string &jobId, const std::string &itemId, const std::string &token, const std::string &reason);
    void cancelItem(const std::string &jobId, const std::string &itemId, const std::string &token);
    void requestCancel(const std::string &jobId);
    // One durable commit for a single-worker job: record the in-flight result,
    // cancel untouched items, and finish. Rejects if any other item is running.
    void cancelAndFinish(const std::string &jobId, const std::optional<CancelledItem> &item = {});
    void finish(const std::string &jobId);
    [[nodiscard]] std::uint64_t retryFailed(const std::string &jobId);
    // Requeue only previously unstarted items after exclusive-owner recovery.
    void resumePending(const std::string &jobId);

    // Caller must hold the host's exclusive execution lease and prove the old
    // worker is gone. In-flight effects become unknown, never automatic failures.
    void recoverInterrupted(const std::string &jobId);
    // The host supplies a verified result before an unknown item may be retried.
    void resolveUnknown(const std::string &jobId, const std::string &itemId, bool succeeded,
                        const std::string &evidence, bool retryable = false);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};
}
