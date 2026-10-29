#pragma once
#include "WebSearch.h"

namespace iiGlobalSearch::detail {
struct WebCliOptions {
    WebConfiguration configuration;
    WebRequest request;
    std::filesystem::path seedFile;
};
WebCliOptions parseWebOptions(const std::vector<std::string>& arguments, std::filesystem::path defaultSeeds = {});
std::string serializeWebResponse(const WebProgress& progress, const std::vector<WebDocument>& results);
}
