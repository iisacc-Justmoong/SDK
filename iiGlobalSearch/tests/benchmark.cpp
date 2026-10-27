#include "GlobalSearch.h"
#include <FileCatalog.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

int main(int argc, char** argv) try {
    if (argc != 2) return 2;
    const auto base = fs::absolute(argv[1]) / "global-search-benchmark";
    fs::remove_all(base);
    const auto root = base / "Benchmark.societycontainer";
    fs::create_directories(root / "Files");
    std::vector<iiFileProvider::FileRegistration> registrations;
    for (int i = 0; i < 300; ++i) {
        auto name = "asset-" + std::to_string(i) + ".txt";
        std::ofstream(root / "Files" / name) << "x";
        iiFileProvider::FileMetadata metadata;
        metadata.title = "Benchmark item " + std::to_string(i);
        metadata.keywords = {"needle"};
        registrations.push_back({name, metadata});
    }
    iiFileProvider::FileCatalog catalog(root / "Files");
    (void)catalog.registerFiles(registrations);
    std::atomic<std::int64_t> first{-1};
    auto started = Clock::now();
    iiGlobalSearch::Index index(root, [&](const iiGlobalSearch::Progress& progress) {
        if (progress.state == iiGlobalSearch::State::indexing && progress.indexed && first.load() < 0)
            first.store(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
    });
    started = Clock::now();
    if (!index.start() || !index.waitFor(std::chrono::minutes(2))) throw std::runtime_error("benchmark timed out");
    const auto finished = Clock::now();
    const auto progress = index.progress();
    const auto rows = index.search("needle", 1024);
    if (progress.state != iiGlobalSearch::State::completed || progress.skipped || rows.size() != registrations.size())
        throw std::runtime_error("benchmark inventory incomplete");
    std::cout << "{\"files\":" << rows.size() << ",\"firstResultMs\":" << first.load()
              << ",\"completeMs\":" << std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count()
              << ",\"scope\":\"synthetic pending catalog metadata, warm filesystem; no performance guarantee\"}\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
