#include "Answer.h"
#include <iiSDKCore/Runtime/Runtime.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace iiGlobalSearch {
const Value* Value::find(std::string_view key) const & noexcept {
    const auto* object = get<Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
Value Value::fromCore(const ii::core::Value& value) {
    return std::visit([](const auto& item) -> Value {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) return {};
        else if constexpr (std::is_same_v<T, ii::core::Value::Array>) {
            List result; result.reserve(item.size());
            for (const auto& child : item) result.push_back(fromCore(child));
            return Value(std::move(result));
        } else if constexpr (std::is_same_v<T, ii::core::Value::Object>) {
            Object result; for (const auto& [key, child] : item) result.emplace(key, fromCore(child));
            return Value(std::move(result));
        } else if constexpr (std::is_same_v<T, ii::core::Bytes>) {
            Bytes result; result.reserve(item.size());
            for (const auto byte : item) result.push_back(static_cast<std::uint8_t>(byte));
            return Value(std::move(result));
        } else return Value(item);
    }, value.storage());
}
namespace {
const Asset* assetOf(const Value& value) {
    if (auto p = value.get<Image>()) return &p->content;
    if (auto p = value.get<Video>()) return &p->content;
    if (auto p = value.get<Audio>()) return &p->content;
    if (auto p = value.get<Vector>()) return std::get_if<Asset>(&p->data);
    if (auto p = value.get<Spreadsheet>()) return &p->source;
    if (auto p = value.get<Scene>()) return &p->source;
    return nullptr;
}
Value withBytes(const Value& value, LoadedBytes loaded) {
    auto bytes = std::make_shared<const Bytes>(std::move(loaded.bytes));
    auto set = [&](Asset& asset) { asset.bytes = bytes; asset.loadedMimeType = loaded.mimeType; };
    if (auto p = value.get<Image>()) { auto copy = *p; set(copy.content); return Value(std::move(copy)); }
    if (auto p = value.get<Video>()) { auto copy = *p; set(copy.content); return Value(std::move(copy)); }
    if (auto p = value.get<Audio>()) { auto copy = *p; set(copy.content); return Value(std::move(copy)); }
    if (auto p = value.get<Vector>()) { auto copy = *p; set(std::get<Asset>(copy.data)); return Value(std::move(copy)); }
    if (auto p = value.get<Spreadsheet>()) { auto copy = *p; set(copy.source); return Value(std::move(copy)); }
    auto copy = *value.get<Scene>(); set(copy.source); return Value(std::move(copy));
}
struct TaskState {
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    std::stop_source stop;
    LoadProgress progress;
    std::optional<Value> value;
};
class LiveLoadTask final : public LoadTask {
public:
    explicit LiveLoadTask(std::shared_ptr<TaskState> state) : state_(std::move(state)) {}
    void cancel() noexcept override { state_->stop.request_stop(); }
    bool waitFor(std::chrono::milliseconds timeout) const override {
        std::unique_lock lock(state_->mutex);
        return state_->changed.wait_for(lock, timeout, [&] { return state_->progress.state == LoadState::completed ||
            state_->progress.state == LoadState::failed || state_->progress.state == LoadState::cancelled; });
    }
    LoadProgress progress() const override { std::lock_guard lock(state_->mutex); return state_->progress; }
    std::optional<Value> result() const override { std::lock_guard lock(state_->mutex); return state_->value; }
private:
    std::shared_ptr<TaskState> state_;
};
}
struct MediaLoader::Impl {
    Impl(std::shared_ptr<AssetReader> r, std::size_t workers, std::size_t capacity)
        : reader(std::move(r)), runtime({workers, capacity}) {}
    ~Impl() {
        { std::lock_guard lock(mutex); for (auto& task : tasks) if (auto state = task.lock()) state->stop.request_stop(); }
        runtime.request_stop(ii::runtime::ShutdownMode::drain); (void)runtime.join();
    }
    std::shared_ptr<AssetReader> reader;
    ii::runtime::Runtime runtime;
    std::mutex mutex;
    std::vector<std::weak_ptr<TaskState>> tasks;
};
MediaLoader::MediaLoader(std::shared_ptr<AssetReader> reader, std::size_t workers, std::size_t capacity) {
    if (!reader || !workers || workers > 16 || !capacity || capacity > 1024) throw std::invalid_argument("Invalid media loader configuration");
    impl_ = std::make_unique<Impl>(std::move(reader), workers, capacity);
}
MediaLoader::~MediaLoader() = default;
std::shared_ptr<LoadTask> MediaLoader::submit(Value value, std::size_t maximumBytes) {
    const auto* asset = assetOf(value);
    if (!asset || asset->reference.kind == AssetSource::none || asset->bytes || !maximumBytes || maximumBytes > std::size_t(1024)*1024*1024)
        throw std::invalid_argument("An unloaded asset reference and a byte limit up to 1 GiB are required");
    auto state = std::make_shared<TaskState>();
    auto task = std::make_shared<LiveLoadTask>(state);
    const auto accepted = impl_->runtime.post([state, reader = impl_->reader, value = std::move(value), maximumBytes] {
        { std::lock_guard lock(state->mutex); state->progress.state = LoadState::running; }
        try {
            if (!state->stop.stop_requested()) {
                auto loaded = reader->read(assetOf(value)->reference, maximumBytes, state->stop.get_token());
                std::lock_guard lock(state->mutex);
                if (state->stop.stop_requested()) state->progress.state = LoadState::cancelled;
                else if (!loaded) { state->progress.state = LoadState::failed; state->progress.error = loaded.error(); }
                else if (loaded->bytes.size() > maximumBytes) { state->progress.state = LoadState::failed; state->progress.error = "Asset exceeds byte limit"; }
                else { state->value = withBytes(value, std::move(*loaded)); state->progress.state = LoadState::completed; }
            } else { std::lock_guard lock(state->mutex); state->progress.state = LoadState::cancelled; }
        } catch (const std::exception& e) {
            std::lock_guard lock(state->mutex); state->progress.state = state->stop.stop_requested() ? LoadState::cancelled : LoadState::failed; state->progress.error = e.what();
        } catch (...) {
            std::lock_guard lock(state->mutex); state->progress.state = LoadState::failed; state->progress.error = "Unknown asset load failure";
        }
        state->changed.notify_all();
    }, ii::runtime::Priority::background);
    if (!accepted) return {};
    { std::lock_guard lock(impl_->mutex); std::erase_if(impl_->tasks, [](const auto& item) { return item.expired(); }); impl_->tasks.push_back(state); }
    return task;
}
} // namespace iiGlobalSearch
