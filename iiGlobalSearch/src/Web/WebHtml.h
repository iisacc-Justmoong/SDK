#pragma once
#include "WebSearch.h"
#include <optional>

namespace iiGlobalSearch::detail {
struct ParsedPage {
    WebDocument document;
    std::string searchableDom;
    std::vector<std::string> follow;
    bool noindex = false, nofollow = false;
};
std::optional<std::string> resolveUrl(const std::string& value, const std::string& base = {});
std::string originOf(const std::string& url);
std::string hostOf(const std::string& url);
std::string pathOf(const std::string& url);
ParsedPage parseHtml(const std::string& url, const std::string& html, std::size_t maxNodes, bool includeDom);
}
