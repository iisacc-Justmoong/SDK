#include "GlobalSearch.h"
#include <FileCatalog.h>
#include <iiFileProviderPresentation.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using namespace iiGlobalSearch;
using namespace iiFileProvider;

static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary); out << text;
    check(bool(out), "fixture write failed");
}
static std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static bool contains(const std::vector<Result>& rows, const fs::path& path) {
    for (const auto& row : rows) if (row.relativePath == path) return true;
    return false;
}
struct ReleaseOnExit {
    std::mutex& mutex;
    std::condition_variable& changed;
    bool& release;
    ~ReleaseOnExit() { { std::lock_guard lock(mutex); release = true; } changed.notify_all(); }
};
struct FixtureCleanup {
    fs::path path;
    ~FixtureCleanup() {
        native::Presentation::cancelPending(path);
        std::error_code error; fs::remove_all(path, error);
    }
};

int main(int argc, char** argv) try {
    check(argc == 2, "build root required");
    const auto base = fs::absolute(argv[1]) / "global-search-fixture";
    fs::remove_all(base);
    FixtureCleanup cleanup{base};
    const auto root = base / "Library.societycontainer";
    fs::create_directories(root / "Files");
    fs::create_directories(root / "Models");
    fs::create_directories(base / "export");
    fs::create_directories(base / "outside");
    write(root / "Files" / "catalog.txt", "catalog original bytes");
    write(root / "Files" / "pending.txt", "pending original bytes");
    write(root / "Files" / "portrait.png", "fixture-image");
    for (const auto& name : {"clip.mp4", "sound.wav", "drawing.svg", "sheet.csv", "model.glb"}) write(root / "Files" / name, "payload");
    FileCatalog catalog(root / "Files");
    FileMetadata classified; classified.title = "서울 Portrait"; classified.description = "Summer campaign";
    classified.keywords = {"approved"}; classified.fields = {{"client", "Aurora"}};
    auto first = catalog.registerFile("catalog.txt", classified);
    check(first.metadataWritten, "catalog fixture classification missing");
    FileMetadata imageMeta; imageMeta.title = "Asset Image";
    check(catalog.registerFile("portrait.png", imageMeta).metadataWritten, "image metadata missing");
    for (const auto& name : {"clip.mp4", "sound.wav", "drawing.svg", "sheet.csv", "model.glb"}) {
        FileMetadata assetMeta; assetMeta.title = name;
        check(catalog.registerFile(name, assetMeta).metadataWritten, "typed asset metadata missing");
    }
    FileMetadata pending; pending.title = "Unpublished Nebula"; pending.keywords = {"draft"};
    const auto pendingRows = catalog.registerFiles({{"pending.txt", pending}});
    check(!fs::exists(root / "Files" / "pending.txt.iisacc.json"), "pending must have no companion");
    write(root / "Files" / "서울문서.txt", "unicode filename");
    FileMetadata unicodeName; unicodeName.title = "No city in this title";
    (void)catalog.registerFiles({{"서울문서.txt", unicodeName}});
    write(root / "Files" / ".private.txt", "private data");
    FileMetadata hidden; hidden.title = "HiddenQuasar";
    (void)catalog.registerFiles({{".private.txt", hidden}});
    const auto originalBytes = read(root / "Files" / "catalog.txt");
    const auto originalCompanion = read(root / "Files" / "catalog.txt.iisacc.json");
    const auto pendingBefore = catalog.pendingMetadata();

    write(base / "export" / "portable.txt", "portable bytes");
    FileCatalog exported(base / "export");
    FileMetadata portable; portable.title = "Portable Maple"; portable.keywords = {"unregistered"};
    check(exported.registerFile("portable.txt", portable).metadataWritten, "portable fixture failed");
    fs::copy_file(base / "export" / "portable.txt", root / "Models" / "portable.txt");
    fs::copy_file(base / "export" / "portable.txt.iisacc.json", root / "Models" / "portable.txt.iisacc.json");
    write(root / "Models" / "presentation.txt", "presentation only");
    auto presentation = native::Presentation::prepare(root / "Models" / "presentation.txt");
    check(!presentation.details.name.empty(), "presentation fixture failed");
    write(root / "Models" / "plain.txt", "unclassified and no preview");
    write(root / "Models" / "broken.txt", "bad companion");
    write(root / "Models" / "broken.txt.iisacc.json", "{broken");
    write(base / "outside" / "escape.txt", "outside");
    FileCatalog outside(base / "outside");
    FileMetadata outsideMeta; outsideMeta.title = "EscapeSupernova";
    check(outside.registerFile("escape.txt", outsideMeta).metadataWritten, "outside fixture failed");
    std::error_code ec;
    fs::create_directory_symlink(base / "outside", root / "Files" / "escape", ec);
    write(root / "Models" / "redirected.txt", "no valid local classification");
    ec.clear();
    fs::create_symlink(base / "outside" / "escape.txt.iisacc.json", root / "Models" / "redirected.txt.iisacc.json", ec);
    const bool companionLinkCreated = !ec;

    std::mutex mutex; std::condition_variable changed; bool held = false, release = false;
    Index index(root, [&](const Progress& p) {
        if (p.state == State::indexing && p.indexed > 0) {
            std::unique_lock lock(mutex);
            if (!held) { held = true; changed.notify_all(); changed.wait(lock, [&] { return release; }); }
        }
    });
    ReleaseOnExit ensureRelease{mutex, changed, release};
    SearchProvider& provider = index;
    const auto before = std::chrono::steady_clock::now();
    check(provider.start(), "start rejected");
    check(std::chrono::steady_clock::now() - before < 1s, "start blocked on indexing");
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, 10s, [&] { return held; }), "worker did not publish partial results");
    }
    check(index.progress().state == State::indexing, "worker did not run asynchronously");
    check(!provider.search().empty(), "partial results unavailable during indexing");
    check(!index.start(), "concurrent start accepted");
    { std::lock_guard lock(mutex); release = true; } changed.notify_all();
    check(index.waitFor(30s), "index timed out");
    check(index.progress().state == State::completed, "index failed");
    check(contains(index.search("서울 por"), "Files/catalog.txt"), "Unicode prefix catalog search failed");
    check(contains(index.search("aurora approved"), "Files/catalog.txt"), "catalog metadata search failed");
    check(!index.search("서울문").empty(), "normalized Korean catalog filename search failed");
    auto pendingResult = index.search("nebula");
    check(contains(pendingResult, "Files/pending.txt") && pendingResult.front().metadataPending, "pending metadata not indexed");
    check(contains(index.search("maple"), "Models/portable.txt"), "unregistered companion omitted");
    check(contains(index.search("presentation"), "Models/presentation.txt"), "presentation-only file omitted");
    check(!contains(index.search(), "Models/plain.txt"), "unclassified plain file indexed");
    check(!contains(index.search(), "Models/broken.txt"), "invalid classification indexed");
    check(!contains(index.search(), "Files/escape/escape.txt"), "link escaped container");
    check(index.search("EscapeSupernova").empty(), "outside provider metadata escaped container");
    if (companionLinkCreated)
        check(!contains(index.search(), "Models/redirected.txt"), "redirected metadata companion was accepted");

    auto typed = [&](const std::string& query) {
        auto response = index.searchAnswer(query);
        const auto* list = response.find("results")->get<Value::List>();
        check(list && !list->empty() && list->front().find("value"), "typed local result missing");
        return *list->front().find("value");
    };
    const auto clip = typed("clip"), sound = typed("sound"), drawing = typed("drawing"),
               sheet = typed("sheet"), model = typed("model");
    check(clip.get<Video>() && sound.get<Audio>() && drawing.get<Vector>() &&
          sheet.get<Spreadsheet>() && model.get<Scene>(), "local asset type inference failed");
    const auto answer = provider.searchAnswer("asset image");
    const auto* answers = answer.find("results")->get<Value::List>();
    check(answers && answers->size() == 1 && answers->front().find("classification") &&
          answers->front().find("value") && answers->front().find("value")->get<Image>(), "structured local image or metadata missing");
    const auto imageValue = *answers->front().find("value");
    check(!imageValue.get<Image>()->content.bytes, "local search eagerly loaded image");
    MediaLoader loader(makeLocalAssetReader(root));
    auto load = loader.submit(imageValue, 1024);
    check(load && load->waitFor(5s) && load->progress().state == LoadState::completed, "local image load failed");
    auto loaded = load->result();
    check(loaded && loaded->get<Image>()->content.bytes && loaded->get<Image>()->content.bytes->size() == 13,
          "local payload not returned in memory");
    Image escape; escape.content.reference.kind = AssetSource::society;
    escape.content.reference.container = root; escape.content.reference.relativePath = "Files/escape/escape.txt";
    auto denied = loader.submit(Value(escape), 1024);
    check(denied && denied->waitFor(5s) && denied->progress().state == LoadState::failed, "redirected asset escaped container");
    escape.content.reference.relativePath = "../outside/escape.txt";
    denied = loader.submit(Value(escape), 1024);
    check(denied && denied->waitFor(5s) && denied->progress().state == LoadState::failed, "relative asset escaped container");
#ifndef _WIN32
    check(::mkfifo((root / "Files" / "pipe").c_str(), 0600) == 0, "FIFO fixture creation failed");
    escape.content.reference.relativePath = "Files/pipe";
    auto fifo = loader.submit(Value(escape), 1024);
    check(fifo && fifo->waitFor(2s) && fifo->progress().state == LoadState::failed, "FIFO blocked or was accepted as media");
#endif
    check(index.search("HiddenQuasar").empty(), "hidden catalog entry exposed");
    check(index.progress().skipped > 0, "invalid companion not reported");
    check(index.progress().error.find("broken.txt") != std::string::npos, "invalid companion diagnostic lacks its path");
    check(!fs::exists(root / "Models" / ".iisacc-file-provider"), "read-only indexing created catalog");
    check(read(root / "Files" / "catalog.txt") == originalBytes &&
          read(root / "Files" / "catalog.txt.iisacc.json") == originalCompanion &&
          catalog.pendingMetadata() == pendingBefore, "indexing changed a source or provider journal");
    check(index.search("\" OR *").empty(), "query syntax was not treated as text");

    fs::remove(root / "Files" / "catalog.txt");
    check(!contains(index.search("서울"), "Files/catalog.txt"), "stale deleted result exposed");
    const auto refreshBefore = std::chrono::steady_clock::now();
    check(index.start(), "refresh rejected");
    check(std::chrono::steady_clock::now() - refreshBefore < 1s, "refresh blocked on previous index teardown");
    check(index.waitFor(30s) && index.progress().state == State::completed, "refresh failed");
    check(!contains(index.search(), "Files/catalog.txt"), "refresh retained deleted file");

    Index invalid(base / "outside");
    check(invalid.start() && invalid.waitFor(10s), "invalid root job did not finish");
    check(invalid.progress().state == State::failed, "ordinary directory accepted");
    fs::create_directory_symlink(root, base / "Linked.societycontainer", ec);
    Index redirected(base / "Linked.societycontainer");
    check(redirected.start() && redirected.waitFor(10s) && redirected.progress().state == State::failed,
          "redirected root accepted");

    std::mutex cancelMutex; std::condition_variable cancelChanged; bool cancelHeld = false, cancelRelease = false;
    Index cancelling(root, [&](const Progress& p) {
        if (p.state == State::indexing && p.indexed) {
            std::unique_lock lock(cancelMutex);
            if (!cancelHeld) { cancelHeld = true; cancelChanged.notify_all(); cancelChanged.wait(lock, [&] { return cancelRelease; }); }
        }
    });
    ReleaseOnExit ensureCancelRelease{cancelMutex, cancelChanged, cancelRelease};
    check(cancelling.start(), "cancellation run did not start");
    { std::unique_lock lock(cancelMutex); check(cancelChanged.wait_for(lock, 10s, [&] { return cancelHeld; }), "cancellation worker did not start"); }
    cancelling.cancel();
    { std::lock_guard lock(cancelMutex); cancelRelease = true; } cancelChanged.notify_all();
    check(cancelling.waitFor(10s) && cancelling.progress().state == State::cancelled, "cancellation did not terminate the run");
    check(cancelling.start() && cancelling.waitFor(30s) && cancelling.progress().state == State::completed, "restart after cancellation failed");
    std::cout << "iiGlobalSearch index tests passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
