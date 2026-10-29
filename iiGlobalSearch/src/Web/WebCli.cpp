#include "WebCli.h"
#include <iiJson/Json.h>
#include <iiSDKCore/Core/Text/Utf8.h>
#include <charconv>
#include <stdexcept>

namespace iiGlobalSearch::detail {
namespace {
using ii::json::Value;
Value safeText(const std::string& input) {
    if (ii::core::valid_utf8(input)) return Value(input);
    std::string latin1; latin1.reserve(input.size() * 2);
    for (const unsigned char byte : input) {
        if (byte < 0x80) latin1 += static_cast<char>(byte);
        else { latin1 += static_cast<char>(0xc0 | (byte >> 6)); latin1 += static_cast<char>(0x80 | (byte & 0x3f)); }
    }
    return Value(std::move(latin1));
}
std::size_t number(const std::string& value, const char* name) {
    std::size_t output = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), output);
    if (error != std::errc{} || end != value.data() + value.size()) throw std::invalid_argument(std::string("invalid ") + name);
    return output;
}
const char* name(WebState state) {
    switch (state) {
        case WebState::queued: return "queued";
        case WebState::running: return "running";
        case WebState::completed: return "completed";
        case WebState::cancelled: return "cancelled";
        case WebState::failed: return "failed";
    }
    return "failed";
}
Value::Array pairs(const std::vector<std::pair<std::string, std::string>>& values) {
    Value::Array result;
    for (const auto& [key, value] : values) result.emplace_back(Value::Object{{"name", safeText(key)}, {"value", safeText(value)}});
    return result;
}
Value::Array strings(const std::vector<std::string>& values) {
    Value::Array result; for (const auto& value : values) result.push_back(safeText(value)); return result;
}
}
WebCliOptions parseWebOptions(const std::vector<std::string>& arguments, std::filesystem::path defaultSeeds) {
    if (arguments.empty()) throw std::invalid_argument("web requires a search query");
    WebCliOptions result; result.request.query = arguments.front(); result.seedFile = std::move(defaultSeeds);
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto option = arguments[i];
        auto argument = [&]() -> std::string { if (++i >= arguments.size()) throw std::invalid_argument("missing value for " + option); return arguments[i]; };
        if (option == "--seed") result.request.seeds.push_back(argument());
        else if (option == "--seed-file") result.seedFile = argument();
        else if (option == "--browser") result.configuration.browserExecutable = argument();
        else if (option == "--scratch") result.configuration.scratchDirectory = argument();
        else if (option == "--max-pages") result.request.maxPages = number(argument(), "page count");
        else if (option == "--depth") result.request.maxDepth = number(argument(), "depth");
        else if (option == "--limit") result.request.maxResults = number(argument(), "limit");
        else if (option == "--timeout-ms") result.request.timeout = std::chrono::milliseconds(number(argument(), "timeout"));
        else if (option == "--render-wait-ms") result.request.renderWait = std::chrono::milliseconds(number(argument(), "render wait"));
        else if (option == "--delay-ms") result.request.perOriginDelay = std::chrono::milliseconds(number(argument(), "origin delay"));
        else if (option == "--raw") result.request.renderJavaScript = false;
        else if (option == "--no-dom") result.request.includeDom = false;
        else if (option == "--same-origin") result.request.sameOriginOnly = true;
        else if (option == "--include-default-seeds") result.request.includeDefaultSeeds = true;
        else throw std::invalid_argument("unknown web option: " + option);
    }
    return result;
}
std::string serializeWebResponse(const WebProgress& progress, const std::vector<WebDocument>& pages) {
    Value::Array results;
    for (const auto& page : pages) {
        Value::Array nodes;
        for (const auto& node : page.dom) nodes.emplace_back(Value::Object{
            {"parent", Value(node.parent)}, {"type", safeText(node.type)}, {"name", safeText(node.name)},
            {"text", safeText(node.text)}, {"attributes", Value(pairs(node.attributes))}});
        results.emplace_back(Value::Object{
            {"url", safeText(page.url)}, {"canonicalUrl", safeText(page.canonicalUrl)}, {"title", safeText(page.title)},
            {"description", safeText(page.description)}, {"contentType", safeText(page.contentType)},
            {"status", Value(page.status)}, {"score", Value(page.score)}, {"rendered", Value(page.rendered)},
            {"text", safeText(page.text)}, {"html", safeText(page.html)}, {"domTruncated", Value(page.domTruncated)},
            {"metadata", Value(pairs(page.metadata))}, {"headers", Value(pairs(page.headers))},
            {"links", Value(strings(page.links))}, {"structuredData", Value(strings(page.structuredData))},
            {"matchedFields", Value(strings(page.matchedFields))}, {"dom", Value(std::move(nodes))}});
    }
    Value response(Value::Object{{"state", Value(name(progress.state))}, {"fetched", Value(progress.fetched)},
        {"matched", Value(progress.matched)}, {"skipped", Value(progress.skipped)},
        {"discovered", Value(progress.discovered)}, {"error", safeText(progress.error)},
        {"diagnostics", Value(strings(progress.diagnostics))}, {"truncated", Value(progress.truncated)},
        {"results", Value(std::move(results))}});
    ii::json::Limits limits; limits.output_bytes = 64 * 1024 * 1024; limits.items = 1000000; limits.string_bytes = 8 * 1024 * 1024;
    auto output = ii::json::stringify(response, limits);
    if (!output) throw std::runtime_error("cannot serialize web search response: " + output.error().message);
    return std::move(*output);
}
}
