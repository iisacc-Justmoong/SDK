#include <GlobalSearch.h>
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    iiGlobalSearch::Index search{std::filesystem::path(argv[1])};
    if (!search.start()) return 1;
    while (!search.waitFor(std::chrono::seconds(1)))
        std::cout << "Indexed " << search.progress().indexed << " files\n";
    if (search.progress().state != iiGlobalSearch::State::completed) return 1;
    const auto answer = search.searchAnswer("campaign");
    const auto* entries = answer.find("results");
    const auto* list = entries ? entries->get<iiGlobalSearch::Value::List>() : nullptr;
    if (!list) return 1;
    for (const auto& result : *list) {
        const auto* path = result.find("path");
        if (path && path->get<std::string>()) std::cout << *path->get<std::string>() << '\n';
    }
}
