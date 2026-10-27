#pragma once

#include <iiSDKCore/Core/Value/Value.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace iiGlobalSearch {

// References describe where a result came from; they do not load the payload.
enum class AssetSource { none, society, web, embedded };
struct AssetReference {
    AssetSource kind = AssetSource::none;
    std::filesystem::path container, relativePath;
    std::string url, originatingUrl, mimeType;
};
using Bytes = std::vector<std::uint8_t>;
struct Asset {
    AssetReference reference;
    std::shared_ptr<const Bytes> bytes; // null = not loaded; an empty buffer is a loaded empty asset
    std::string loadedMimeType;
};
struct Image { Asset content; };
struct Video { Asset content; };
struct Audio { Asset content; };
// Numeric vectors and encoded vector graphics are distinct alternatives.
struct Vector { std::variant<std::vector<double>, Asset> data = std::vector<double>{}; };
struct Spreadsheet {
    std::vector<std::vector<ii::core::Value>> rows;
    Asset source; // Optional reference to a spreadsheet document.
};
struct GraphNode { std::string id, label; ii::core::Value attributes; };
struct GraphEdge { std::string from, to, label; ii::core::Value attributes; };
struct GraphPoint { std::vector<double> coordinates; ii::core::Value attributes; };
struct GraphSeries { std::string label; std::vector<GraphPoint> points; ii::core::Value attributes; };
struct Graph { std::vector<GraphNode> nodes; std::vector<GraphEdge> edges; std::vector<GraphSeries> series; };
struct SceneNode {
    std::string id, parent;
    std::array<double,16> transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    ii::core::Value attributes;
};
struct Scene { std::vector<SceneNode> nodes; Asset source; };

// Recursive heterogeneous, owned answer; no file or serialization is involved.
class Value final {
public:
    using List = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;
    using Storage = std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double,
                                 std::string, Bytes, List, Object, Image, Video, Audio,
                                 Vector, Spreadsheet, Graph, Scene>;
    Value() = default;
    Value(const char* value) : data_(std::string(value ? value : "")) {}
    Value(int value) : data_(static_cast<std::int64_t>(value)) {}
    template<class T> requires (!std::is_same_v<std::remove_cvref_t<T>, Value> &&
                                std::is_constructible_v<Storage, T>)
    explicit Value(T&& value) : data_(std::forward<T>(value)) {}
    template<class T> [[nodiscard]] const T* get() const & noexcept { return std::get_if<T>(&data_); }
    template<class T> const T* get() const && = delete;
    [[nodiscard]] const Value* find(std::string_view key) const & noexcept;
    const Value* find(std::string_view) const && = delete;
    [[nodiscard]] const Storage& storage() const & noexcept { return data_; }
    const Storage& storage() const && = delete;
    [[nodiscard]] static Value fromCore(const ii::core::Value& value);
private:
    Storage data_;
};
using Answer = Value; // Root is an Object containing source, state and results.

struct LoadedBytes { Bytes bytes; std::string mimeType; };
class AssetReader {
public:
    virtual ~AssetReader() = default;
    // Implementations must honour the limit and stop request; loader checks the limit again.
    [[nodiscard]] virtual std::expected<LoadedBytes, std::string> read(
        const AssetReference&, std::size_t maximumBytes, std::stop_token) = 0;
};
enum class LoadState { queued, running, completed, cancelled, failed };
struct LoadProgress { LoadState state = LoadState::queued; std::string error; };
class LoadTask {
public:
    virtual ~LoadTask() = default;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual bool waitFor(std::chrono::milliseconds) const = 0;
    [[nodiscard]] virtual LoadProgress progress() const = 0;
    [[nodiscard]] virtual std::optional<Value> result() const = 0;
};
// Uses a bounded reusable iiSDKCore worker pool. Destruction cancels and joins jobs.
class MediaLoader final {
public:
    explicit MediaLoader(std::shared_ptr<AssetReader> reader, std::size_t workers = 2,
                         std::size_t capacity = 16);
    ~MediaLoader();
    MediaLoader(const MediaLoader&) = delete;
    MediaLoader& operator=(const MediaLoader&) = delete;
    // Empty pointer means queue full. Throws for a non-asset value or invalid limit.
    [[nodiscard]] std::shared_ptr<LoadTask> submit(Value value, std::size_t maximumBytes = 32 * 1024 * 1024);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iiGlobalSearch
