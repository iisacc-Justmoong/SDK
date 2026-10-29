#include "GlobalSearch.h"
#include "WebSearch.h"
#include "WebCli.h"
#include <iiJson/Json.h>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <string>

namespace {
using ii::json::Value;
std::string text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
const char* stateName(iiGlobalSearch::State state) {
    switch (state) {
        case iiGlobalSearch::State::idle: return "idle";
        case iiGlobalSearch::State::indexing: return "indexing";
        case iiGlobalSearch::State::completed: return "completed";
        case iiGlobalSearch::State::cancelled: return "cancelled";
        case iiGlobalSearch::State::failed: return "failed";
    }
    return "failed";
}
std::vector<std::string> readSeeds(const std::filesystem::path& path) {
    std::ifstream input(path); if (!input) throw std::runtime_error("cannot read seed file: " + text(path));
    std::vector<std::string> seeds; std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.front() != '#') seeds.push_back(line);
        if (seeds.size() > 1000) throw std::runtime_error("seed file exceeds its limit");
    }
    return seeds;
}
std::filesystem::path defaultSeedFile(const char* command) {
    if (const auto* configured = std::getenv("IIGLOBALSEARCH_SEED_FILE")) return configured;
    std::filesystem::path executable(command);
    if (!executable.has_parent_path()) {
        if (const auto* path = std::getenv("PATH")) {
            std::string entries(path); std::size_t start = 0;
#ifdef _WIN32
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif
            while (start <= entries.size()) {
                const auto end = entries.find(separator, start);
                const auto entry = entries.substr(start, end == entries.npos ? entries.npos : end - start);
                if (!entry.empty() && std::filesystem::is_regular_file(std::filesystem::path(entry) / executable)) {
                    executable = std::filesystem::path(entry) / executable; break;
                }
                if (end == entries.npos) break;
                start = end + 1;
            }
        }
    }
    executable = std::filesystem::absolute(executable);
    const auto base = executable.parent_path();
    for (auto candidate : {std::filesystem::current_path() / "resources/web-seeds.txt",
                           base.parent_path() / "resources/web-seeds.txt",
                           base.parent_path() / "share/iiGlobalSearch/web-seeds.txt"})
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    return {};
}
int webCommand(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int i = 2; i < argc; ++i) arguments.emplace_back(argv[i]);
    auto options = iiGlobalSearch::detail::parseWebOptions(arguments, defaultSeedFile(argv[0]));
    if (!options.seedFile.empty() && (options.request.seeds.empty() || options.request.includeDefaultSeeds))
        options.configuration.defaultSeeds = readSeeds(options.seedFile);
    iiGlobalSearch::WebSearch search(std::move(options.configuration));
    auto task = search.submit(std::move(options.request));
    if (!task) throw std::runtime_error("web work queue is full");
    while (!task->waitFor(std::chrono::seconds(1))) {}
    const auto progress = task->progress();
    std::cout << iiGlobalSearch::detail::serializeWebResponse(progress, task->results()) << '\n';
    return progress.state == iiGlobalSearch::WebState::completed ? 0 : 1;
}
}

int main(int argc, char** argv) try {
    if (argc >= 2 && std::string(argv[1]) == "web") return webCommand(argc, argv);
    if (argc < 3 || argc > 6 || std::string(argv[1]) != "index") {
        std::cerr << "Usage: ii-global-search index <absolute.societycontainer> [text] [--limit 1..1024]\n"
                     "       ii-global-search web <query> [--seed URL] [--seed-file FILE] [--max-pages N] [--depth N] [--raw]\n";
        return 2;
    }
    std::string query;
    std::size_t limit = 100;
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--limit" && i + 1 < argc) {
            const std::string number = argv[++i];
            const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), limit);
            if (error != std::errc{} || end != number.data() + number.size() || limit == 0 || limit > 1024)
                throw std::invalid_argument("limit must be between 1 and 1024");
        } else if (query.empty()) query = argv[i];
        else throw std::invalid_argument("unexpected command argument");
    }
    iiGlobalSearch::Index index{std::filesystem::path(argv[2])};
    if (!index.start()) throw std::runtime_error("could not start indexing");
    while (!index.waitFor(std::chrono::seconds(1))) {}
    const auto progress = index.progress();
    Value::Array results;
    for (const auto& item : index.search(query, limit)) {
        results.emplace_back(Value::Object{
            {"path", Value(text(item.relativePath))}, {"title", Value(item.title)},
            {"fileKey", Value(item.fileKey)}, {"mimeType", Value(item.mimeType)},
            {"size", Value(item.size)}, {"hasClassification", Value(item.hasClassification)},
            {"hasPresentation", Value(item.hasPresentation)}, {"metadataPending", Value(item.metadataPending)},
            {"classification", Value(item.classification)}});
    }
    const Value response(Value::Object{
        {"state", Value(stateName(progress.state))}, {"directories", Value(progress.directories)},
        {"files", Value(progress.files)}, {"indexed", Value(progress.indexed)},
        {"skipped", Value(progress.skipped)}, {"error", Value(progress.error)},
        {"results", Value(std::move(results))}});
    const auto json = ii::json::stringify(response);
    if (!json) throw std::runtime_error("cannot serialize search response");
    std::cout << *json << '\n';
    return progress.state == iiGlobalSearch::State::completed ? 0 : 1;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
