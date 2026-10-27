#include <GlobalSearch.h>
#include <WebSearch.h>
#include <chrono>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    iiGlobalSearch::Index index{std::filesystem::path(argv[1])};
    if (!index.start() || !index.waitFor(std::chrono::seconds(30))) return 3;
    const auto progress = index.progress();
    if (progress.state != iiGlobalSearch::State::completed) return 4;
    const auto answer = index.searchAnswer("maple");
    if (!answer.find("results") || !answer.find("results")->get<iiGlobalSearch::Value::List>()) return 7;
    for (const auto& result : index.search("maple")) {
        if (result.relativePath == std::filesystem::path("Models/portable.txt")) {
            iiGlobalSearch::WebSearch web;
            iiGlobalSearch::WebRequest request; request.seeds = {"file:///invalid"}; request.renderJavaScript = false;
            auto task = web.submit(request);
            if (!task || !task->waitFor(std::chrono::seconds(10)) || task->progress().state != iiGlobalSearch::WebState::failed) return 6;
            const auto webAnswer = task->answer();
            if (!webAnswer.find("results")) return 8;
            std::cout << "Installed consumer found portable classification\n";
            return 0;
        }
    }
    return 5;
}
