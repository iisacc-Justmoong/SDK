#include "Answer.h"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace iiGlobalSearch;
using namespace std::chrono_literals;
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class Reader final : public AssetReader {
public:
    std::mutex mutex; std::condition_variable changed; bool entered = false, release = false;
    std::expected<LoadedBytes, std::string> read(const AssetReference&, std::size_t,
                                                std::stop_token stop) override {
        std::unique_lock lock(mutex); entered = true; changed.notify_all();
        while (!release && !stop.stop_requested()) changed.wait_for(lock, 10ms);
        if (stop.stop_requested()) return std::unexpected("cancelled");
        return LoadedBytes{{1,2,3}, "image/png"};
    }
};
int main() try {
    Value::List values{Value(std::int64_t(-2)), Value(2.5), Value("text"), Value(Image{}),
        Value(Video{}), Value(Audio{}), Value(Vector{std::vector<double>{1,2}}),
        Value(Vector{Asset{}}), Value(Spreadsheet{}), Value(Graph{}), Value(Scene{})};
    Value answer(Value::Object{{"results", Value(values)}, {"ready", Value(true)}});
    check(answer.find("results") && answer.find("results")->get<Value::List>()->size() == 11,
          "heterogeneous recursive values missing");
    check((*answer.find("results")->get<Value::List>())[0].get<std::int64_t>() &&
          (*answer.find("results")->get<Value::List>())[6].get<Vector>() &&
          (*answer.find("results")->get<Value::List>())[10].get<Scene>(), "exact value types missing");
    ii::core::Value::Object core{{"integer", ii::core::Value(std::int64_t(-3))},
        {"nested", ii::core::Value(ii::core::Value::Array{ii::core::Value(true), ii::core::Value("hello")})}};
    const auto converted = Value::fromCore(ii::core::Value(std::move(core)));
    check(converted.find("integer") && *converted.find("integer")->get<std::int64_t>() == -3 &&
          converted.find("nested")->get<Value::List>()->size() == 2, "iiSDKCore value conversion lost types");
    Spreadsheet spreadsheet; spreadsheet.rows = {{ii::core::Value("label"), ii::core::Value(std::int64_t(42))}};
    Graph graph; graph.nodes.push_back({"one", "node", {}}); graph.edges.push_back({"one", "two", "edge", {}});
    graph.series.push_back({"measurements", {{{1.0, 2.0}, {}}}, {}});
    Scene scene; scene.nodes.push_back({"object", "", {}, {}});
    Value types(Value::List{Value(std::move(spreadsheet)), Value(std::move(graph)), Value(std::move(scene))});
    const auto* typeList = types.get<Value::List>();
    check((*typeList)[0].get<Spreadsheet>()->rows.size() == 1 && (*typeList)[1].get<Graph>()->edges.size() == 1 &&
          (*typeList)[1].get<Graph>()->series.front().points.front().coordinates.size() == 2 &&
          (*typeList)[2].get<Scene>()->nodes.size() == 1, "structured spatial or tabular values lost data");
    auto reader = std::make_shared<Reader>(); MediaLoader loader(reader, 1, 2);
    Image image; image.content.reference.kind = AssetSource::web; image.content.reference.url = "https://example.test/a.png";
    Value original(image); auto started = std::chrono::steady_clock::now();
    auto task = loader.submit(original, 16);
    check(task && std::chrono::steady_clock::now() - started < 1s, "load submission blocked");
    { std::unique_lock lock(reader->mutex); check(reader->changed.wait_for(lock, 2s, [&] { return reader->entered; }), "reader did not start"); }
    check(!original.get<Image>()->content.bytes && !task->result(), "media loaded eagerly");
    { std::lock_guard lock(reader->mutex); reader->release = true; } reader->changed.notify_all();
    check(task->waitFor(2s) && task->progress().state == LoadState::completed, "load did not complete");
    const auto loaded = task->result();
    check(loaded && loaded->get<Image>() && loaded->get<Image>()->content.bytes &&
          loaded->get<Image>()->content.bytes->size() == 3 && !original.get<Image>()->content.bytes,
          "load did not produce a separate in-memory snapshot");
    Video video; video.content.reference = image.content.reference;
    Audio audio; audio.content.reference = image.content.reference;
    auto videoTask = loader.submit(Value(video), 16);
    auto audioTask = loader.submit(Value(audio), 16);
    check(videoTask && audioTask && videoTask->waitFor(2s) && audioTask->waitFor(2s) &&
          videoTask->result()->get<Video>()->content.bytes && audioTask->result()->get<Audio>()->content.bytes,
          "video or audio did not preserve its type after loading");
    auto limited = loader.submit(original, 2); check(limited && limited->waitFor(2s) &&
        limited->progress().state == LoadState::failed && !limited->result(), "reader exceeded the requested byte limit");
    { std::lock_guard lock(reader->mutex); reader->release = false; reader->entered = false; }
    auto cancelled = loader.submit(original, 16); check(bool(cancelled), "cancel load not accepted");
    { std::unique_lock lock(reader->mutex); check(reader->changed.wait_for(lock, 2s, [&] { return reader->entered; }), "cancel reader did not start"); }
    auto queued = loader.submit(Value(audio), 16); check(bool(queued), "queued load not accepted");
    queued->cancel(); cancelled->cancel();
    check(cancelled->waitFor(2s) && cancelled->progress().state == LoadState::cancelled && !cancelled->result(), "cancel failed");
    check(queued->waitFor(2s) && queued->progress().state == LoadState::cancelled, "queued cancellation failed");
    bool rejected = false; try { (void)loader.submit(Value("not an asset"), 16); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "invalid media type accepted");
    std::cout << "iiGlobalSearch answer tests passed\n"; return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
