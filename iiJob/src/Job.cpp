#include "Job.h"

#include <sqlite3.h>
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace ii::job {
namespace {
namespace fs = std::filesystem;
constexpr int applicationId = 0x694a6f62;
constexpr int schemaVersion = 1;
constexpr std::size_t maxItems = 250000;
[[noreturn]] void raise(const std::string &message) { throw std::runtime_error("iiJob: " + message); }
std::string pathText(const fs::path &path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
bool utf8(std::string_view value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i]);
        if (first < 0x80) { ++i; continue; }
        unsigned count = 0; std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 2; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 3; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 4; code = first & 0x07; minimum = 0x10000; }
        else return false;
        if (i + count > value.size()) return false;
        for (unsigned n = 1; n < count; ++n) {
            const auto next = static_cast<unsigned char>(value[i+n]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
        i += count;
    }
    return true;
}
void text(const std::string &value, std::size_t maximum, bool required = false) {
    if ((required && value.empty()) || value.size() > maximum || value.find('\0') != std::string::npos || !utf8(value))
        raise("invalid or oversized UTF-8 field");
}
std::int64_t number(std::uint64_t value) {
    if (value > std::uint64_t(std::numeric_limits<std::int64_t>::max())) raise("counter exceeds storage range");
    return static_cast<std::int64_t>(value);
}
void check(sqlite3 *db, int code) {
    if (code != SQLITE_OK && code != SQLITE_DONE && code != SQLITE_ROW)
        raise(db ? sqlite3_errmsg(db) : "SQLite error");
}
void exec(sqlite3 *db, const char *sql) { check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr)); }
class Statement {
public:
    Statement(sqlite3 *db, const char *sql) : db_(db) { check(db_, sqlite3_prepare_v2(db_, sql, -1, &statement_, nullptr)); }
    ~Statement() { sqlite3_finalize(statement_); }
    void bind(int position, const std::string &value) {
        check(db_, sqlite3_bind_text(statement_, position, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void bind(int position, std::int64_t value) { check(db_, sqlite3_bind_int64(statement_, position, value)); }
    void null(int position) { check(db_, sqlite3_bind_null(statement_, position)); }
    bool row() { const auto result = sqlite3_step(statement_); check(db_, result); return result == SQLITE_ROW; }
    std::string string(int column) const {
        const auto *value = sqlite3_column_text(statement_, column);
        return value ? std::string(reinterpret_cast<const char *>(value), std::size_t(sqlite3_column_bytes(statement_, column))) : std::string{};
    }
    std::int64_t integer(int column) const { return sqlite3_column_int64(statement_, column); }
    bool isNull(int column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }
private:
    sqlite3 *db_;
    sqlite3_stmt *statement_ = nullptr;
};
class Transaction {
public:
    explicit Transaction(sqlite3 *db) : db_(db) { exec(db_, "BEGIN IMMEDIATE"); }
    ~Transaction() { if (!committed_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(db_, "COMMIT"); committed_ = true; }
private:
    sqlite3 *db_; bool committed_ = false;
};
std::int64_t scalar(sqlite3 *db, const char *sql) {
    Statement query(db, sql);
    if (!query.row()) raise("missing database metadata");
    return query.integer(0);
}
void requireChange(sqlite3 *db, const char *message) { if (sqlite3_changes(db) != 1) raise(message); }
JobState jobState(std::int64_t value) {
    if (value < 0 || value > static_cast<int>(JobState::attention)) raise("invalid stored job state");
    return static_cast<JobState>(value);
}
ItemState itemState(std::int64_t value) {
    if (value < 0 || value > static_cast<int>(ItemState::skipped)) raise("invalid stored item state");
    return static_cast<ItemState>(value);
}
std::uint64_t nonnegative(std::int64_t value) { if (value < 0) raise("invalid stored counter"); return std::uint64_t(value); }

// Require a pre-existing, direct regular file. SQLite may create only its own
// journal/WAL sidecars; allocation of the database itself belongs to the host.
fs::path checkedPath(const fs::path &path) {
    if (!path.is_absolute() || path.lexically_normal() != path) raise("an absolute normalized database path is required");
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec || !fs::is_regular_file(status) || fs::is_symlink(status) || fs::canonical(path, ec) != path || ec)
        raise("database is missing or redirected");
#ifdef _WIN32
    auto current = path.root_path();
    for (const auto &part : path.relative_path()) {
        current /= part;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) raise("database path is redirected");
    }
#endif
    for (const auto *suffix : {"-wal", "-shm", "-journal"}) {
        auto sidecar=path; sidecar += suffix; ec.clear();
        const auto sideStatus=fs::symlink_status(sidecar,ec);
        if (ec == std::errc::no_such_file_or_directory || (!ec && !fs::exists(sideStatus))) continue;
        if (ec || fs::is_symlink(sideStatus) || !fs::is_regular_file(sideStatus)) raise("database sidecar is redirected or invalid");
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(sidecar.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) raise("database sidecar is redirected");
#endif
    }
    return path;
}
constexpr const char *schema = R"SQL(
CREATE TABLE jobs(
 id TEXT PRIMARY KEY, kind TEXT NOT NULL, requester TEXT NOT NULL, resource TEXT NOT NULL,
 base_revision TEXT NOT NULL, target_node TEXT NOT NULL, cancellable INTEGER NOT NULL CHECK(cancellable IN(0,1)),
 actual_node TEXT NOT NULL DEFAULT '', stage TEXT NOT NULL DEFAULT '', reason TEXT NOT NULL DEFAULT '',
 state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 9), retry_count INTEGER NOT NULL DEFAULT 0 CHECK(retry_count>=0),
 processed INTEGER NOT NULL DEFAULT 0 CHECK(processed>=0), total_units INTEGER CHECK(total_units>=0), unit TEXT NOT NULL DEFAULT '');
CREATE TABLE items(
 job_id TEXT NOT NULL REFERENCES jobs(id), id TEXT NOT NULL, source TEXT NOT NULL, resource TEXT NOT NULL,
 base_revision TEXT NOT NULL, label TEXT NOT NULL, state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 6), attempts INTEGER NOT NULL DEFAULT 0 CHECK(attempts>=0),
 token TEXT NOT NULL DEFAULT '', checkpoint TEXT NOT NULL DEFAULT '', artifact TEXT NOT NULL DEFAULT '',
 error TEXT NOT NULL DEFAULT '', retryable INTEGER NOT NULL DEFAULT 0 CHECK(retryable IN(0,1)), PRIMARY KEY(job_id,id));
CREATE INDEX items_by_state ON items(job_id,state,id);
CREATE TABLE attempts(
 job_id TEXT NOT NULL, item_id TEXT NOT NULL, number INTEGER NOT NULL CHECK(number>0), state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 6),
 checkpoint TEXT NOT NULL DEFAULT '', artifact TEXT NOT NULL DEFAULT '', error TEXT NOT NULL DEFAULT '', node TEXT NOT NULL,
 retryable INTEGER NOT NULL DEFAULT 0 CHECK(retryable IN(0,1)), PRIMARY KEY(job_id,item_id,number),
 FOREIGN KEY(job_id,item_id) REFERENCES items(job_id,id));
)SQL";
}

class Store::Impl {
public:
    explicit Impl(const fs::path &file) {
        const auto path = checkedPath(file);
        const bool wasEmpty = fs::file_size(path) == 0;
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
        flags |= SQLITE_OPEN_NOFOLLOW;
#endif
        const auto name = pathText(path);
        const int opened = sqlite3_open_v2(name.c_str(), &db, flags, nullptr);
        if (opened != SQLITE_OK) {
            const auto error = db ? std::string(sqlite3_errmsg(db)) : std::string("cannot open database");
            if (db) sqlite3_close(db);
            db = nullptr; raise(error);
        }
        try {
            sqlite3_busy_timeout(db, 5000);
            exec(db, "PRAGMA foreign_keys=ON");
            {
                Transaction transaction(db);
                const auto id = scalar(db, "PRAGMA application_id"), version = scalar(db, "PRAGMA user_version");
                if (id == 0 && version == 0 && wasEmpty &&
                    scalar(db, "SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'") == 0) {
                    exec(db, schema);
                    exec(db, "PRAGMA application_id=1766485858; PRAGMA user_version=1");
                } else if (id != applicationId || version != schemaVersion) raise("foreign or unsupported job database");
                transaction.commit();
            }
            exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL");
        } catch (...) { sqlite3_close(db); db = nullptr; throw; }
    }
    ~Impl() { if (db) sqlite3_close(db); }
    sqlite3 *db = nullptr;
    mutable std::mutex mutex;

    std::optional<Job> loadJob(const std::string &id) const {
        Statement query(db, R"SQL(SELECT id,kind,requester,resource,base_revision,target_node,actual_node,stage,reason,unit,
 state,retry_count,processed,total_units,cancellable,
 (SELECT count(*) FROM items WHERE job_id=jobs.id),
 (SELECT count(*) FROM items WHERE job_id=jobs.id AND state=0),
 (SELECT count(*) FROM items WHERE job_id=jobs.id AND state=1),
 (SELECT count(*) FROM items WHERE job_id=jobs.id AND state=2),
 (SELECT count(*) FROM items WHERE job_id=jobs.id AND state=3),
 (SELECT count(*) FROM items WHERE job_id=jobs.id AND state=4),
 (SELECT count(*) FROM items WHERE job_id=jobs.id AND state=5),
 (SELECT count(*) FROM items WHERE job_id=jobs.id AND state=6)
 FROM jobs WHERE id=?)SQL");
        query.bind(1, id); if (!query.row()) return {};
        Job result;
        result.id=query.string(0); result.kind=query.string(1); result.requester=query.string(2); result.resource=query.string(3);
        result.baseRevision=query.string(4); result.targetNode=query.string(5); result.actualNode=query.string(6);
        result.stage=query.string(7); result.reason=query.string(8); result.unit=query.string(9); result.state=jobState(query.integer(10));
        result.retryCount=nonnegative(query.integer(11)); result.processed=nonnegative(query.integer(12));
        if (!query.isNull(13)) result.totalUnits=nonnegative(query.integer(13)); result.cancellable=query.integer(14)!=0;
        result.total=nonnegative(query.integer(15)); result.pending=nonnegative(query.integer(16)); result.running=nonnegative(query.integer(17));
        result.succeeded=nonnegative(query.integer(18)); result.failed=nonnegative(query.integer(19)); result.cancelled=nonnegative(query.integer(20));
        result.unknown=nonnegative(query.integer(21)); result.skipped=nonnegative(query.integer(22)); return result;
    }
    std::optional<Item> loadItem(const std::string &jobId, const std::string &itemId) const {
        Statement query(db, "SELECT id,source,resource,base_revision,state,attempts,checkpoint,artifact,error,retryable,label FROM items WHERE job_id=? AND id=?");
        query.bind(1, jobId); query.bind(2, itemId); if (!query.row()) return {};
        Item item; item.id=query.string(0); item.source=query.string(1); item.resource=query.string(2); item.baseRevision=query.string(3);
        item.state=itemState(query.integer(4)); item.attempts=nonnegative(query.integer(5)); item.checkpoint=query.string(6);
        item.artifact=query.string(7); item.error=query.string(8); item.retryable=query.integer(9)!=0; item.label=query.string(10); return item;
    }
    void complete(const std::string &job, const std::string &item, const std::string &token,
                  ItemState state, const std::string &artifact, const std::string &error, bool retryable) {
        text(job,512,true); text(item,512,true); text(token,512,true); text(artifact,65536); text(error,65536);
        std::lock_guard lock(mutex); Transaction transaction(db);
        Statement update(db, R"SQL(UPDATE items SET state=?,token='',artifact=?,error=?,retryable=?
 WHERE job_id=? AND id=? AND token=? AND state=1 AND EXISTS(SELECT 1 FROM jobs WHERE id=? AND state IN(3,6)))SQL");
        update.bind(1,std::int64_t(state)); update.bind(2,artifact); update.bind(3,error); update.bind(4,std::int64_t(retryable));
        update.bind(5,job); update.bind(6,item); update.bind(7,token); update.bind(8,job); (void)update.row();
        requireChange(db, "item claim is no longer valid");
        Statement attempt(db, R"SQL(UPDATE attempts SET state=?,artifact=?,error=?,retryable=?
 WHERE job_id=? AND item_id=? AND number=(SELECT attempts FROM items WHERE job_id=? AND id=?))SQL");
        attempt.bind(1,std::int64_t(state)); attempt.bind(2,artifact); attempt.bind(3,error); attempt.bind(4,std::int64_t(retryable));
        attempt.bind(5,job); attempt.bind(6,item); attempt.bind(7,job); attempt.bind(8,item); (void)attempt.row();
        requireChange(db, "attempt record is missing"); transaction.commit();
    }
};

Store::Store(const fs::path &path) : d(std::make_unique<Impl>(path)) {}
Store::~Store() = default;
void Store::create(const JobRequest &request, const std::vector<ItemRequest> &items) {
    text(request.id,512,true); text(request.kind,512,true); text(request.requester,512,true);
    text(request.resource,4096); text(request.baseRevision,4096); text(request.targetNode,4096);
    if (items.empty() || items.size() > maxItems) raise("job requires between 1 and 250000 items");
    std::unordered_set<std::string> ids;
    for (const auto &item : items) {
        text(item.id,512,true); text(item.source,16384); text(item.resource,4096); text(item.baseRevision,4096); text(item.label,4096);
        if (!ids.insert(item.id).second) raise("duplicate item id");
    }
    std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement insert(d->db,"INSERT INTO jobs(id,kind,requester,resource,base_revision,target_node,cancellable,state) VALUES(?,?,?,?,?,?,?,0)");
    insert.bind(1,request.id); insert.bind(2,request.kind); insert.bind(3,request.requester); insert.bind(4,request.resource);
    insert.bind(5,request.baseRevision); insert.bind(6,request.targetNode); insert.bind(7,std::int64_t(request.cancellable)); (void)insert.row();
    for (const auto &item : items) {
        Statement row(d->db,"INSERT INTO items(job_id,id,source,resource,base_revision,label,state) VALUES(?,?,?,?,?,?,0)");
        row.bind(1,request.id); row.bind(2,item.id); row.bind(3,item.source); row.bind(4,item.resource); row.bind(5,item.baseRevision); row.bind(6,item.label); (void)row.row();
    }
    transaction.commit();
}
std::optional<Job> Store::job(const std::string &id) const { text(id,512,true); std::lock_guard lock(d->mutex); return d->loadJob(id); }
std::vector<Job> Store::jobs(const std::string &after, std::size_t limit) const {
    text(after,512); if (!limit || limit > 1024) raise("invalid page limit");
    std::lock_guard lock(d->mutex); Statement query(d->db,"SELECT id FROM jobs WHERE id>? ORDER BY id LIMIT ?");
    query.bind(1,after); query.bind(2,std::int64_t(limit)); std::vector<Job> result;
    while (query.row()) if (auto value=d->loadJob(query.string(0))) result.push_back(std::move(*value)); return result;
}
std::vector<Job> Store::recent(std::size_t limit) const {
    if (!limit || limit > 1024) raise("invalid page limit");
    std::lock_guard lock(d->mutex); Statement query(d->db,"SELECT id FROM jobs ORDER BY rowid DESC LIMIT ?");
    query.bind(1,std::int64_t(limit)); std::vector<Job> result;
    while (query.row()) if (auto value=d->loadJob(query.string(0))) result.push_back(std::move(*value)); return result;
}
std::optional<Item> Store::item(const std::string &jobId, const std::string &itemId) const {
    text(jobId,512,true); text(itemId,512,true); std::lock_guard lock(d->mutex); return d->loadItem(jobId,itemId);
}
std::vector<Item> Store::items(const std::string &jobId, const std::string &after, std::size_t limit) const {
    text(jobId,512,true); text(after,512); if (!limit || limit>1024) raise("invalid page limit");
    std::lock_guard lock(d->mutex); Statement query(d->db,"SELECT id FROM items WHERE job_id=? AND id>? ORDER BY id LIMIT ?");
    query.bind(1,jobId); query.bind(2,after); query.bind(3,std::int64_t(limit)); std::vector<Item> result;
    while (query.row()) if (auto value=d->loadItem(jobId,query.string(0))) result.push_back(std::move(*value)); return result;
}
std::vector<Attempt> Store::history(const std::string &jobId, const std::string &itemId) const {
    text(jobId,512,true); text(itemId,512,true); std::lock_guard lock(d->mutex);
    Statement query(d->db,"SELECT number,state,checkpoint,artifact,error,node,retryable FROM attempts WHERE job_id=? AND item_id=? ORDER BY number");
    query.bind(1,jobId); query.bind(2,itemId); std::vector<Attempt> result;
    while (query.row()) result.push_back({nonnegative(query.integer(0)),itemState(query.integer(1)),query.string(2),query.string(3),query.string(4),query.string(5),query.integer(6)!=0});
    return result;
}
void Store::setWaiting(const std::string &id, JobState state, const std::string &reason) {
    text(id,512,true); text(reason,4096,true);
    if (state != JobState::waiting && state != JobState::waitingNode && state != JobState::waitingPermission) raise("invalid waiting state");
    std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement query(d->db,"UPDATE jobs SET state=?,reason=? WHERE id=? AND state IN(0,1,2,3) AND NOT EXISTS(SELECT 1 FROM items WHERE job_id=? AND state=1)");
    query.bind(1,std::int64_t(state)); query.bind(2,reason); query.bind(3,id); query.bind(4,id); (void)query.row();
    requireChange(d->db,"cannot wait while a job is terminal or an item is running"); transaction.commit();
}
void Store::begin(const std::string &id, const std::string &node, const std::string &stage) {
    text(id,512,true); text(node,4096,true); text(stage,4096,true);
    std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement query(d->db,"UPDATE jobs SET state=3,actual_node=?,stage=?,reason='' WHERE id=? AND state IN(0,1,2) AND EXISTS(SELECT 1 FROM items WHERE job_id=? AND state=0)");
    query.bind(1,node); query.bind(2,stage); query.bind(3,id); query.bind(4,id); (void)query.row();
    requireChange(d->db,"job is not ready to begin"); transaction.commit();
}
void Store::progress(const std::string &id, const std::string &stage, std::uint64_t processed,
                     std::optional<std::uint64_t> total, const std::string &unit) {
    text(id,512,true); text(stage,4096,true); text(unit,512);
    if (total && processed>*total) raise("progress exceeds total");
    std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement query(d->db,"UPDATE jobs SET stage=?,processed=?,total_units=?,unit=? WHERE id=? AND state=3");
    query.bind(1,stage); query.bind(2,number(processed)); if (total) query.bind(3,number(*total)); else query.null(3);
    query.bind(4,unit); query.bind(5,id); (void)query.row(); requireChange(d->db,"job is not running"); transaction.commit();
}
Claim Store::claim(const std::string &jobId, const std::string &itemId) {
    text(jobId,512,true); text(itemId,512,true); std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    const auto previous=d->loadItem(jobId,itemId); if (!previous) raise("unknown item");
    Statement random(d->db,"SELECT lower(hex(randomblob(16)))"); if (!random.row()) raise("claim token unavailable"); const auto token=random.string(0);
    Statement query(d->db,R"SQL(UPDATE items SET state=1,attempts=attempts+1,token=?,checkpoint='',artifact='',error='',retryable=0
 WHERE job_id=? AND id=? AND state=0 AND EXISTS(SELECT 1 FROM jobs WHERE id=? AND state=3))SQL");
    query.bind(1,token); query.bind(2,jobId); query.bind(3,itemId); query.bind(4,jobId); (void)query.row();
    requireChange(d->db,"item is not pending or job is not running");
    const auto next=previous->attempts+1;
    Statement attempt(d->db,"INSERT INTO attempts(job_id,item_id,number,state,node) SELECT ?,?,?,1,actual_node FROM jobs WHERE id=?");
    attempt.bind(1,jobId); attempt.bind(2,itemId); attempt.bind(3,number(next)); attempt.bind(4,jobId); (void)attempt.row();
    requireChange(d->db,"job disappeared while claiming an item"); transaction.commit();
    return {token,previous->checkpoint,next};
}
void Store::checkpoint(const std::string &jobId, const std::string &itemId, const std::string &token, const std::string &value) {
    text(jobId,512,true); text(itemId,512,true); text(token,512,true); text(value,65536);
    std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement update(d->db,"UPDATE items SET checkpoint=? WHERE job_id=? AND id=? AND token=? AND state=1");
    update.bind(1,value); update.bind(2,jobId); update.bind(3,itemId); update.bind(4,token); (void)update.row(); requireChange(d->db,"item claim is no longer valid");
    Statement attempt(d->db,"UPDATE attempts SET checkpoint=? WHERE job_id=? AND item_id=? AND number=(SELECT attempts FROM items WHERE job_id=? AND id=?)");
    attempt.bind(1,value); attempt.bind(2,jobId); attempt.bind(3,itemId); attempt.bind(4,jobId); attempt.bind(5,itemId); (void)attempt.row();
    requireChange(d->db,"attempt record is missing"); transaction.commit();
}
void Store::succeed(const std::string &j, const std::string &i, const std::string &t, const std::string &artifact) { d->complete(j,i,t,ItemState::succeeded,artifact,{},false); }
void Store::fail(const std::string &j, const std::string &i, const std::string &t, const std::string &error, bool retryable) {
    text(error,65536,true); d->complete(j,i,t,ItemState::failed,{},error,retryable);
}
void Store::skip(const std::string &j, const std::string &i, const std::string &t, const std::string &reason) { d->complete(j,i,t,ItemState::skipped,{},reason,false); }
void Store::cancelItem(const std::string &j, const std::string &i, const std::string &t) { d->complete(j,i,t,ItemState::cancelled,{},"cancelled",false); }
void Store::requestCancel(const std::string &id) {
    text(id,512,true); std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement job(d->db,"UPDATE jobs SET state=6,reason='cancel requested' WHERE id=? AND cancellable=1 AND state IN(0,1,2,3)");
    job.bind(1,id); (void)job.row(); requireChange(d->db,"job cannot be cancelled");
    Statement items(d->db,"UPDATE items SET state=4,error='cancelled before start' WHERE job_id=? AND state=0"); items.bind(1,id); (void)items.row();
    transaction.commit();
}
void Store::cancelAndFinish(const std::string &id, const std::optional<CancelledItem> &item) {
    text(id,512,true);
    if (item) {
        text(item->id,512,true); text(item->token,512,true); text(item->artifact,65536); text(item->error,65536);
        if (item->state != ItemState::cancelled && item->state != ItemState::succeeded && item->state != ItemState::failed)
            raise("invalid cancellation outcome");
        if ((item->state == ItemState::failed && item->error.empty()) ||
            (item->state != ItemState::failed && item->retryable) ||
            (item->state == ItemState::succeeded && !item->error.empty()) ||
            (item->state != ItemState::succeeded && !item->artifact.empty()))
            raise("invalid cancellation result");
    }
    std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    {
        Statement existing(d->db,"SELECT state,cancellable FROM jobs WHERE id=?");
        existing.bind(1,id);
        if (!existing.row() || !existing.integer(1)) raise("job cannot be cancelled");
        const auto state = jobState(existing.integer(0));
        if (state != JobState::waiting && state != JobState::waitingNode && state != JobState::waitingPermission &&
            state != JobState::running && state != JobState::cancelRequested)
            raise("job cannot be cancelled");
    }
    if (item) {
        const auto error = item->state == ItemState::cancelled && item->error.empty() ? std::string("cancelled") : item->error;
        Statement update(d->db, "UPDATE items SET state=?,token='',artifact=?,error=?,retryable=? WHERE job_id=? AND id=? AND token=? AND state=1");
        update.bind(1,std::int64_t(item->state)); update.bind(2,item->artifact); update.bind(3,error);
        update.bind(4,std::int64_t(item->retryable)); update.bind(5,id); update.bind(6,item->id); update.bind(7,item->token);
        (void)update.row(); requireChange(d->db,"item claim is no longer valid");
        Statement attempt(d->db, R"SQL(UPDATE attempts SET state=?,artifact=?,error=?,retryable=? WHERE job_id=? AND item_id=?
 AND number=(SELECT attempts FROM items WHERE job_id=? AND id=?))SQL");
        attempt.bind(1,std::int64_t(item->state)); attempt.bind(2,item->artifact); attempt.bind(3,error);
        attempt.bind(4,std::int64_t(item->retryable)); attempt.bind(5,id); attempt.bind(6,item->id);
        attempt.bind(7,id); attempt.bind(8,item->id); (void)attempt.row(); requireChange(d->db,"attempt record is missing");
    }
    {
        Statement running(d->db,"SELECT count(*) FROM items WHERE job_id=? AND state=1");
        running.bind(1,id); if (!running.row() || running.integer(0) != 0) raise("other items are still running");
    }
    Statement pending(d->db,"UPDATE items SET state=4,error='cancelled before start' WHERE job_id=? AND state=0");
    pending.bind(1,id); (void)pending.row();
    Statement finish(d->db,"UPDATE jobs SET state=7,reason='cancelled' WHERE id=?");
    finish.bind(1,id); (void)finish.row(); requireChange(d->db,"job disappeared while cancelling");
    transaction.commit();
}
void Store::finish(const std::string &id) {
    text(id,512,true); std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    const auto value=d->loadJob(id); if (!value || (value->state!=JobState::running && value->state!=JobState::cancelRequested && value->state!=JobState::attention)) raise("job cannot be finished");
    if (value->pending || value->running) raise("unfinished items remain");
    JobState state=JobState::succeeded;
    if (value->state==JobState::cancelRequested) state=JobState::cancelled;
    else if (value->unknown) state=JobState::attention;
    else if (value->failed) state=(value->succeeded || value->skipped) ? JobState::partial : JobState::failed;
    else if (value->cancelled) state=JobState::cancelled;
    Statement query(d->db,"UPDATE jobs SET state=?,reason='' WHERE id=?"); query.bind(1,std::int64_t(state)); query.bind(2,id); (void)query.row(); transaction.commit();
}
std::uint64_t Store::retryFailed(const std::string &id) {
    text(id,512,true); std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    const auto value=d->loadJob(id);
    if (!value || (value->state!=JobState::partial && value->state!=JobState::failed && value->state!=JobState::attention && value->state!=JobState::cancelled))
        raise("job is not eligible for retry");
    Statement items(d->db,"UPDATE items SET state=0,error='',retryable=0 WHERE job_id=? AND state=3 AND retryable=1"); items.bind(1,id); (void)items.row();
    const auto count=std::uint64_t(sqlite3_changes(d->db));
    if (count) { Statement update(d->db,"UPDATE jobs SET state=0,retry_count=retry_count+1,reason='' WHERE id=?"); update.bind(1,id); (void)update.row(); }
    transaction.commit(); return count;
}
void Store::resumePending(const std::string &id) {
    text(id,512,true); std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement query(d->db,"UPDATE jobs SET state=0,reason='' WHERE id=? AND state=9 AND EXISTS(SELECT 1 FROM items WHERE job_id=? AND state=0)");
    query.bind(1,id); query.bind(2,id); (void)query.row();
    requireChange(d->db,"job has no recoverable pending items"); transaction.commit();
}
void Store::recoverInterrupted(const std::string &id) {
    text(id,512,true); std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement job(d->db,"UPDATE jobs SET state=9,reason='interrupted; verify in-flight results' WHERE id=? AND state IN(3,6)");
    job.bind(1,id); (void)job.row(); requireChange(d->db,"job is not interrupted");
    Statement attempts(d->db,"UPDATE attempts SET state=5 WHERE job_id=? AND state=1"); attempts.bind(1,id); (void)attempts.row();
    Statement items(d->db,"UPDATE items SET state=5,token='' WHERE job_id=? AND state=1"); items.bind(1,id); (void)items.row();
    transaction.commit();
}
void Store::resolveUnknown(const std::string &jobId, const std::string &itemId, bool succeeded,
                           const std::string &evidence, bool retryable) {
    text(jobId,512,true); text(itemId,512,true); text(evidence,65536,true);
    const auto state=succeeded ? ItemState::succeeded : ItemState::failed;
    std::lock_guard lock(d->mutex); Transaction transaction(d->db);
    Statement update(d->db,R"SQL(UPDATE items SET state=?,artifact=?,error=?,retryable=? WHERE job_id=? AND id=? AND state=5
 AND EXISTS(SELECT 1 FROM jobs WHERE id=? AND state=9))SQL");
    update.bind(1,std::int64_t(state)); update.bind(2,succeeded?evidence:std::string{}); update.bind(3,succeeded?std::string{}:evidence);
    update.bind(4,std::int64_t(!succeeded && retryable)); update.bind(5,jobId); update.bind(6,itemId); update.bind(7,jobId); (void)update.row();
    requireChange(d->db,"item is not awaiting verification");
    Statement attempt(d->db,R"SQL(UPDATE attempts SET state=?,artifact=?,error=?,retryable=? WHERE job_id=? AND item_id=?
 AND number=(SELECT attempts FROM items WHERE job_id=? AND id=?))SQL");
    attempt.bind(1,std::int64_t(state)); attempt.bind(2,succeeded?evidence:std::string{}); attempt.bind(3,succeeded?std::string{}:evidence);
    attempt.bind(4,std::int64_t(!succeeded && retryable)); attempt.bind(5,jobId); attempt.bind(6,itemId); attempt.bind(7,jobId); attempt.bind(8,itemId);
    (void)attempt.row(); requireChange(d->db,"attempt record is missing"); transaction.commit();
}
} // namespace ii::job
