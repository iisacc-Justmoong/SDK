#include "LabelAdapter.h"
#include "LocalizedLabel.generated.h"
#include "WidgetState.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using State=ii::gui::detail::WidgetState;
using View=lvui_generated::LocalizedLabel;
using ii::localizer::LabelUsage;

// Surface errors from the independent host interfaces in a single test report.
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) throw std::runtime_error(value.error().message);
}
template<class T> T take(ii::core::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}

// Read one actual native label snapshot without opening an OS window.
std::string label(const std::shared_ptr<State>& tree,std::string_view id) {
    auto nodes=take(tree->snapshots());
    const auto found=std::ranges::find_if(nodes,[&](const auto& node){return node.id.ends_with(id);});
    require(found!=nodes.end(),"Generated LVUI label not found");
    return take(take(tree->find(found->id)).snapshot()).text;
}

// Exercise scoped and global publication through real generated LVUI bindings.
void exercise_labels() {
    std::ifstream file(IILOCALIZER_TEST_PO_PATH,std::ios::binary);
    require(bool(file),"Cannot read the Korean PO fixture");
    const std::string po{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    auto ko=ii::localizer::Catalog::parse(po);
    auto en=ii::localizer::Catalog::parse("");
    require(ko && en,"Fixture catalogs must parse");
    auto strings=std::make_shared<lvui::native::AppStrings>();
    good(strings->replace_catalog("ko",{{"other.label","보존"}}));
    const std::vector<LabelUsage> uses{
        {"main/toolbar/toolbarGreeting","Hello, %1","toolbar.greeting"},
        {"main/panel/panelGreeting","Hello, %1","panel.greeting"},
        {"main/toolbar/toolbarOpen","Open","toolbar.open"},
        {"main/panel/panelOpen","Open","panel.open"},
        {"main/panel/other","Other","other.label"},
    };
    LabelAdapter adapter(strings,"Hello, %1",uses);
    LabelAdapter open_adapter(strings,"Open",uses);
    const std::array<std::string,1> first{"지민"};
    good(adapter.publish("en",*en,first));
    good(adapter.publish("ko",*ko,first));
    good(open_adapter.publish("en",*en,{}));
    good(open_adapter.publish("ko",*ko,{}));
    auto legacy_strings=std::make_shared<lvui::native::AppStrings>();
    LabelAdapter legacy(legacy_strings,"Hello, %1");
    good(legacy.publish("en",*en,first));
    require(!legacy.publish("en",*en,first,"main/toolbar/toolbarGreeting"),
            "A legacy shared key cannot promise exact location targeting");
    require(legacy_strings->get("Hello, %1")=="Hello, 지민","Legacy global publication changed unexpectedly");
    auto factories=lvui::native::BackendFactories{}.with_app_strings(strings);
    auto tree=std::make_shared<State>([](ii::runtime::Task task){task();},View::appearance());
    std::shared_ptr<View::Scope> scope;
    good(tree->construct([&](auto& builder){return View::build(builder,{}, {},&scope,factories);}));
    require(label(tree,"toolbarGreeting")=="Hello, 지민" && label(tree,"panelGreeting")=="Hello, 지민",
            "Global source publication must reach both labels");
    good(strings->set_locale("ko-KR"));
    require(label(tree,"toolbarGreeting")=="안녕하세요, 지민님" && label(tree,"panelGreeting")=="안녕하세요, 지민님",
            "Global Korean publication must reach both labels");
    require(label(tree,"other")=="보존","A different label was unexpectedly changed");
    require(label(tree,"toolbarOpen")=="개방" && label(tree,"panelOpen")=="개방",
            "Context-free PO translations must reach both Open labels");
    good(open_adapter.publish("ko",*ko,{},"main/toolbar/toolbarOpen","menu"));
    require(label(tree,"toolbarOpen")=="열기" && label(tree,"panelOpen")=="개방",
            "PO context and exact location must be applied independently");
    good(open_adapter.publish("ko",*ko,{},"main/panel/panelOpen",""));
    require(label(tree,"toolbarOpen")=="열기" && label(tree,"panelOpen")=="빈 문맥",
            "Empty PO context must remain distinct from no context");
    const std::array<std::string,1> second{"민서"};
    good(adapter.publish("ko",*ko,second,"main/panel/panelGreeting"));
    require(label(tree,"panelGreeting")=="안녕하세요, 민서님" && label(tree,"toolbarGreeting")=="안녕하세요, 지민님",
            "Exact location must change only the selected label");
    require(!adapter.publish("ko",*ko,second,"missing/greeting"),"Unknown location must report no match");
    require(!adapter.publish("ko",*ko,second,"main/panel/other"),"Different source at the same location must not match");
    require(!adapter.publish("ko",*ko,{}),"Missing placeholder argument was accepted");
    require(!adapter.publish("ja",*ko,second),"Mismatched PO language was accepted");
    auto duplicates=uses; duplicates[1].key=duplicates[0].key;
    LabelAdapter invalid(strings,"Hello, %1",duplicates);
    require(!invalid.publish("ko",*ko,second,"main/toolbar/toolbarGreeting"),"Shared keys must be rejected");
    require(label(tree,"panelGreeting")=="안녕하세요, 민서님" && label(tree,"toolbarGreeting")=="안녕하세요, 지민님",
            "A rejected selection changed an active label");
    const std::array<std::string,1> third{"수현"};
    good(adapter.publish("ko",*ko,third));
    require(label(tree,"toolbarGreeting")=="안녕하세요, 수현님" && label(tree,"panelGreeting")=="안녕하세요, 수현님",
            "Omitting location must update all matching active labels");
    require(label(tree,"other")=="보존","Global source selection changed a different label");
    require(label(tree,"toolbarOpen")=="열기" && label(tree,"panelOpen")=="빈 문맥",
            "Global greeting replacement changed labels with a different source");
    good(strings->set_locale("en"));
    require(label(tree,"toolbarGreeting")=="Hello, 지민" && label(tree,"panelGreeting")=="Hello, 지민",
            "Language switching must preserve the other catalog");
    tree->close(); scope.reset();
}
}

// This executable is a headless test consumer, not a launched product application.
int main() {
    try { exercise_labels(); }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
