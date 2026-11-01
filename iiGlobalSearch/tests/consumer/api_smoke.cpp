#include <Answer.h>
#include <GlobalSearch.h>
#include <WebSearch.h>
#include <chrono>
#include <stdexcept>
#include <string>
#include <type_traits>

static_assert(std::is_constructible_v<iiGlobalSearch::Index, std::filesystem::path>);
int main() {
    using namespace iiGlobalSearch;
    Value answer(Value::Object{{"source", Value("fixture")}});
    const auto *source = answer.find("source");
    if (!source || !source->get<std::string>() || *source->get<std::string>() != "fixture") return 1;
    ObjectSession session;
    FileObject object; object.id="fixture.txt"; object.providerId="provider-key"; object.title="Consumer fixture";
    object.metadata.keywords={"approved"}; object.metadata.rating=4;
    session.store(std::move(object));
    ObjectFilter filter; filter.text="consumer"; filter.allKeywords={"approved"}; filter.minimumRating=4;
    const auto objects=session.search(filter);
    if (objects.size()!=1 || objects.front().providerId!="provider-key") return 4;
    session.destroy();
    if (!session.destroyed()) return 5;
    try { (void)session.search(); return 6; } catch (const std::logic_error&) {}
    WebSearch web; WebRequest request; request.seeds = {"file:///invalid"}; request.renderJavaScript = false;
    auto task = web.submit(request);
    if (!task || !task->waitFor(std::chrono::seconds(10)) || task->progress().state != WebState::failed) return 2;
    const auto result = task->answer();
    return result.find("results") ? 0 : 3;
}
