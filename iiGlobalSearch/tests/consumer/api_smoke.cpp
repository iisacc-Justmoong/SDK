#include <Answer.h>
#include <GlobalSearch.h>
#include <WebSearch.h>
#include <chrono>
#include <string>
#include <type_traits>

static_assert(std::is_constructible_v<iiGlobalSearch::Index, std::filesystem::path>);
int main() {
    using namespace iiGlobalSearch;
    Value answer(Value::Object{{"source", Value("fixture")}});
    const auto *source = answer.find("source");
    if (!source || !source->get<std::string>() || *source->get<std::string>() != "fixture") return 1;
    WebSearch web; WebRequest request; request.seeds = {"file:///invalid"}; request.renderJavaScript = false;
    auto task = web.submit(request);
    if (!task || !task->waitFor(std::chrono::seconds(10)) || task->progress().state != WebState::failed) return 2;
    const auto result = task->answer();
    return result.find("results") ? 0 : 3;
}
