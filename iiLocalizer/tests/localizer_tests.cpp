#include <iiLocalizer>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using ii::localizer::Catalog;
using ii::localizer::Origin;

// Report a failing contract without relying on assertions disabled in Release.
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

// Read the source-controlled PO fixture from resources, never from the build tree.
std::string fixture() {
    std::ifstream input(IILOCALIZER_TEST_PO_PATH, std::ios::binary);
    require(bool(input), "Cannot open Korean PO fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Check real PO data, exact contexts, missing translations and interpolation.
void translations() {
    auto catalog=Catalog::parse(fixture());
    require(bool(catalog), "A valid UTF-8 PO must parse");
    require(catalog->language()=="ko", "Language metadata must be available");
    const std::array<std::string,1> name{"지민"};
    auto greeting=catalog->translate("Hello, %1",name);
    require(greeting && greeting->text=="안녕하세요, 지민님" && greeting->origin==Origin::translated,
            "Translated greeting must reach the caller with its argument");
    const std::array<std::string,2> pair{"A","B"};
    auto reordered=catalog->translate("First %1, second %2",pair);
    require(reordered && reordered->text=="두 번째 B, 첫 번째 A", "Translations may reorder arguments");
    auto context=catalog->translate("Open",{},"menu");
    auto empty_context=catalog->translate("Open",{},"");
    auto absent_context=catalog->translate("Open");
    require(context && context->text=="열기" && empty_context && empty_context->text=="빈 문맥" &&
            absent_context && absent_context->text=="개방", "Absent, empty and named contexts differ");
    for (const auto id:{"Unreviewed","Unreviewed too","Untranslated","Obsolete","Missing"}) {
        auto result=catalog->translate(id);
        require(result && result->text==id && result->origin==Origin::source_missing,
                "Missing, fuzzy and obsolete translations must fall back to source");
    }
    auto escaped=catalog->translate("Escapes");
    require(escaped && escaped->text=="첫째 줄\n둘째 줄: \"인용\"", "Multiline and escapes must decode");
    auto invalid=catalog->translate("Malformed %1",name);
    require(invalid && invalid->text=="Malformed 지민" && invalid->origin==Origin::source_invalid_translation,
            "Invalid translated placeholders must fall back to a valid source");
    auto failure=catalog->translate("Need %2",name);
    require(!failure, "A missing argument in the source is an error");
    const std::array<std::string,10> ten{"%2","2","3","4","5","6","7","8","9","10"};
    auto positional=catalog->translate("%10 %% %1 100%",ten);
    require(positional && positional->text=="10 % %2 100%", "Placeholders are greedy and inserted values are literal");
    require(!catalog->translate("%0",{}), "%0 must be rejected");
    require(!catalog->translate("%9999999999999999999999999999999",{}), "Overflow must be rejected");
}

// Malformed catalogs must fail as a whole instead of publishing partial data.
void invalid_catalogs() {
    const auto rejects=[](std::string_view input){require(!Catalog::parse(input),"Invalid PO must be rejected");};
    rejects("msgid \"a\"\nmsgstr \"b\"\n\nmsgid \"a\"\nmsgstr \"c\"\n");
    rejects("msgid \"a\"\nmsgid_plural \"as\"\nmsgstr[0] \"b\"\n");
    rejects("msgid \"a\"\nmsgstr \"bad\\q\"\n");
    rejects("msgid \"a\"\nmsgstr \"unfinished\n");
    rejects("msgid \"a\"\nmsgstr \"\\0\"\n");
    rejects("msgid \"\xff\"\nmsgstr \"x\"\n");
    rejects("msgid \"\"\nmsgstr \"Content-Type: text/plain; charset=ISO-8859-1\\n\"\n");
    rejects(std::string(8*1024*1024+1,'x'));
}

// Select exact label locations without confusing equal source strings or keys.
void usage_selection() {
    using ii::localizer::LabelUsage;
    using ii::localizer::select_label_usages;
    const std::vector<LabelUsage> uses{
        {"main/toolbar/toolbarGreeting","Hello, %1","toolbar.greeting"},
        {"main/panel/panelGreeting","Hello, %1","panel.greeting"},
        {"main/panel/other","Other","footer.other"},
    };
    auto all=select_label_usages(uses,"Hello, %1");
    require(all && *all==std::vector<std::size_t>{0,1},"Omitted location must select every matching source in order");
    auto one=select_label_usages(uses,"Hello, %1","main/panel/panelGreeting");
    require(one && *one==std::vector<std::size_t>{1},"An exact location must select only its own label");
    auto wrong=select_label_usages(uses,"Hello, %1","main/panel/other");
    require(wrong && wrong->empty(),"A matching location with a different source must not be selected");
    auto missing=select_label_usages(uses,"Hello, %1","missing/greeting");
    require(missing && missing->empty(),"An unknown location must not silently select every label");
    auto prefix=select_label_usages(uses,"Hello, %1","main/panel");
    auto different_case=select_label_usages(uses,"Hello, %1","main/Panel/panelGreeting");
    require(prefix && prefix->empty() && different_case && different_case->empty(),
            "Locations must match exactly, without prefix or case folding");
    require(!select_label_usages(uses,"Hello, %1",""),"An empty location is invalid, not omitted");
    auto duplicate_location=uses;
    duplicate_location[1].location=duplicate_location[0].location;
    require(!select_label_usages(duplicate_location,"Hello, %1"),"Duplicate locations must be rejected");
    auto duplicate_key=uses;
    duplicate_key[1].key=duplicate_key[0].key;
    require(!select_label_usages(duplicate_key,"Hello, %1","main/toolbar/toolbarGreeting"),
            "Shared keys cannot support independent targeting");
    auto empty_source=uses;
    empty_source[2].source.clear();
    require(!select_label_usages(empty_source,"Hello, %1"),"Invalid unrelated registrations must not be ignored");
    const std::vector<LabelUsage> none;
    auto empty=select_label_usages(none,"Hello, %1");
    require(empty && empty->empty(),"An empty registry has no matches");
}
}

// Run the native contract checks; the fixture path is supplied by CMake.
int main() {
    try { translations(); invalid_catalogs(); usage_selection(); }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
