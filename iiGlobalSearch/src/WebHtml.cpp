#include "WebHtml.h"
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace iiGlobalSearch::detail {
namespace {
using Uri = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
using Doc = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
std::string xmlText(const xmlChar* s) { return s ? reinterpret_cast<const char*>(s) : ""; }
std::optional<std::string> urlPart(CURLU* uri, CURLUPart part, unsigned flags = 0) {
    char* value = nullptr;
    if (curl_url_get(uri, part, &value, flags) != CURLUE_OK) return {};
    std::string output(value ? value : ""); curl_free(value); return output;
}
Uri parseUrl(const std::string& text) {
    Uri uri(curl_url(), &curl_url_cleanup);
    if (!uri || curl_url_set(uri.get(), CURLUPART_URL, text.c_str(), 0) != CURLUE_OK) return {nullptr, &curl_url_cleanup};
    return uri;
}
std::string content(xmlNodePtr node) {
    auto* value = xmlNodeGetContent(node); auto output = xmlText(value); xmlFree(value); return output;
}
std::string attribute(xmlNodePtr node, const char* name) {
    auto* value = xmlGetProp(node, reinterpret_cast<const xmlChar*>(name));
    auto output = xmlText(value); xmlFree(value); return output;
}
bool word(const std::string& input, const std::string& target) {
    auto value = lower(input);
    for (auto& c : value) if (c == ',' || c == ';') c = ' ';
    std::istringstream stream(value); std::string part;
    while (stream >> part) if (part == target) return true;
    return false;
}
void appendText(std::string& output, const std::string& value) {
    if (value.empty()) return;
    if (!output.empty() && !std::isspace(static_cast<unsigned char>(output.back()))) output += ' ';
    output += value;
}
}

std::optional<std::string> resolveUrl(const std::string& value, const std::string& base) {
    if (value.empty() || value.size() > 8192 || value.find('\0') != value.npos) return {};
    Uri uri(curl_url(), &curl_url_cleanup);
    if (!uri || (!base.empty() && curl_url_set(uri.get(), CURLUPART_URL, base.c_str(), 0) != CURLUE_OK) ||
        curl_url_set(uri.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return {};
    const auto schemePart = urlPart(uri.get(), CURLUPART_SCHEME);
    const auto hostPart = urlPart(uri.get(), CURLUPART_HOST);
    if (!schemePart || !hostPart || hostPart->empty() || urlPart(uri.get(), CURLUPART_USER) ||
        urlPart(uri.get(), CURLUPART_PASSWORD) || urlPart(uri.get(), CURLUPART_OPTIONS)) return {};
    const auto scheme = lower(*schemePart);
    if (scheme != "http" && scheme != "https") return {};
    const auto host = lower(*hostPart);
    if (curl_url_set(uri.get(), CURLUPART_SCHEME, scheme.c_str(), 0) != CURLUE_OK ||
        curl_url_set(uri.get(), CURLUPART_HOST, host.c_str(), 0) != CURLUE_OK ||
        curl_url_set(uri.get(), CURLUPART_FRAGMENT, nullptr, 0) != CURLUE_OK) return {};
    return urlPart(uri.get(), CURLUPART_URL, CURLU_NO_DEFAULT_PORT);
}
std::string originOf(const std::string& url) {
    auto uri = parseUrl(url);
    if (!uri) return {};
    const auto scheme = urlPart(uri.get(), CURLUPART_SCHEME), host = urlPart(uri.get(), CURLUPART_HOST);
    if (!scheme || !host) return {};
    std::string origin = lower(*scheme) + "://" + lower(*host);
    if (const auto port = urlPart(uri.get(), CURLUPART_PORT, CURLU_NO_DEFAULT_PORT)) origin += ':' + *port;
    return origin;
}
std::string hostOf(const std::string& url) {
    auto uri = parseUrl(url); if (!uri) return {};
    const auto host = urlPart(uri.get(), CURLUPART_HOST); return host ? lower(*host) : std::string{};
}
std::string pathOf(const std::string& url) {
    auto uri = parseUrl(url);
    if (!uri) return "/";
    std::string path = urlPart(uri.get(), CURLUPART_PATH).value_or("/");
    if (const auto query = urlPart(uri.get(), CURLUPART_QUERY)) path += '?' + *query;
    return path;
}

ParsedPage parseHtml(const std::string& url, const std::string& html, std::size_t maxNodes, bool includeDom) {
    ParsedPage out; out.document.url = url;
    if (html.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("HTML is too large");
    Doc doc(htmlReadMemory(html.data(), static_cast<int>(html.size()), url.c_str(), nullptr,
                           HTML_PARSE_RECOVER | HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING), &xmlFreeDoc);
    if (!doc) throw std::runtime_error("Cannot parse HTML DOM");
    if (doc->encoding) xmlFree(const_cast<xmlChar*>(doc->encoding));
    doc->encoding = xmlStrdup(BAD_CAST "UTF-8");
    xmlChar* serialized = nullptr; int serializedSize = 0;
    htmlDocDumpMemory(doc.get(), &serialized, &serializedSize);
    if (!serialized || serializedSize < 0) { if (serialized) xmlFree(serialized); throw std::runtime_error("Cannot serialize HTML DOM"); }
    out.document.html.assign(reinterpret_cast<const char*>(serialized), static_cast<std::size_t>(serializedSize));
    xmlFree(serialized);
    std::string base = url;
    std::function<bool(xmlNodePtr, int)> findBase = [&](xmlNodePtr n, int depth) {
        if (depth > 256) return false;
        for (; n; n = n->next) {
            if (n->type == XML_ELEMENT_NODE && lower(xmlText(n->name)) == "base") {
                if (const auto resolved = resolveUrl(attribute(n, "href"), url)) { base = *resolved; return true; }
            }
            if (findBase(n->children, depth + 1)) return true;
        }
        return false;
    };
    (void)findBase(xmlDocGetRootElement(doc.get()), 0);
    std::size_t scanned = 0, tableCells = 0, inlineBytes = 0;
    auto addAsset = [&](Value value) {
        if (out.document.assets.size() < 1024) out.document.assets.push_back(std::move(value));
        else out.document.assetsTruncated = true;
    };
    std::function<void(xmlNodePtr, std::int64_t, bool, int)> walk = [&](xmlNodePtr node, std::int64_t parent, bool visible, int depth) {
        if (depth > 256) { out.document.domTruncated = true; return; }
        for (; node && scanned < 100000; node = node->next) {
            ++scanned;
            const auto name = node->type == XML_ELEMENT_NODE ? lower(xmlText(node->name)) : std::string{};
            const bool textVisible = visible && name != "script" && name != "style" && name != "noscript" && name != "template";
            std::vector<std::pair<std::string, std::string>> attributes;
            if (node->type == XML_ELEMENT_NODE) {
                appendText(out.searchableDom, name);
                for (auto* attr = node->properties; attr; attr = attr->next) {
                    auto* value = xmlNodeListGetString(doc.get(), attr->children, 1);
                    attributes.emplace_back(xmlText(attr->name), xmlText(value)); xmlFree(value);
                    appendText(out.searchableDom, attributes.back().first);
                    appendText(out.searchableDom, attributes.back().second);
                }
            }
            std::int64_t nextParent = parent;
            if (includeDom && (node->type == XML_ELEMENT_NODE || node->type == XML_TEXT_NODE || node->type == XML_COMMENT_NODE)) {
                if (out.document.dom.size() < maxNodes) {
                    WebNode item; item.parent = parent;
                    if (node->type == XML_ELEMENT_NODE) {
                        item.type = "element"; item.name = name;
                        item.attributes = std::move(attributes);
                    } else { item.type = node->type == XML_TEXT_NODE ? "text" : "comment"; item.text = xmlText(node->content); }
                    nextParent = static_cast<std::int64_t>(out.document.dom.size());
                    out.document.dom.push_back(std::move(item));
                } else out.document.domTruncated = true;
            }
            if (node->type == XML_TEXT_NODE && textVisible) appendText(out.document.text, xmlText(node->content));
            if (name == "title" && out.document.title.empty()) out.document.title = content(node);
            if (name == "meta") {
                auto key = attribute(node, "name"); if (key.empty()) key = attribute(node, "property");
                if (key.empty()) key = attribute(node, "http-equiv");
                auto value = attribute(node, "content");
                if (!key.empty()) {
                    out.document.metadata.emplace_back(key, value);
                    if (lower(key) == "description" && out.document.description.empty()) out.document.description = value;
                    if (lower(key) == "robots" || lower(key) == "iiglobalsearch") {
                        if (word(value, "noindex") || word(value, "none")) out.noindex = true;
                        if (word(value, "nofollow") || word(value, "none")) out.nofollow = true;
                    }
                }
            }
            if (name == "script" && lower(attribute(node, "type")) == "application/ld+json")
                out.document.structuredData.push_back(content(node));
            if (name == "link" && word(attribute(node, "rel"), "canonical")) {
                if (auto link = resolveUrl(attribute(node, "href"), base)) out.document.canonicalUrl = *link;
            }
            if (name == "a") {
                if (auto link = resolveUrl(attribute(node, "href"), base)) {
                    out.document.links.push_back(*link);
                    if (!word(attribute(node, "rel"), "nofollow") && !word(attribute(node, "rel"), "ugc") && !word(attribute(node, "rel"), "sponsored")) out.follow.push_back(*link);
                }
            }
            auto addUrlAsset = [&](const std::string& source, const std::string& type) {
                auto resolved = resolveUrl(source, base);
                if (!resolved) return;
                Asset asset; asset.reference.kind = AssetSource::web;
                asset.reference.url = *resolved; asset.reference.originatingUrl = url;
                asset.reference.mimeType = attribute(node, "type");
                const auto path = lower(pathOf(*resolved));
                if (type == "image" && (path.ends_with(".svg") || path.find(".svg?") != std::string::npos || lower(asset.reference.mimeType).starts_with("image/svg")))
                    addAsset(Value(Vector{std::move(asset)}));
                else if (type == "image") addAsset(Value(Image{std::move(asset)}));
                else if (type == "video") addAsset(Value(Video{std::move(asset)}));
                else if (type == "audio") addAsset(Value(Audio{std::move(asset)}));
                else if (type == "spreadsheet") { Spreadsheet sheet; sheet.source = std::move(asset); addAsset(Value(std::move(sheet))); }
                else if (type == "scene") { Scene scene; scene.source = std::move(asset); addAsset(Value(std::move(scene))); }
            };
            if (name == "img") addUrlAsset(attribute(node, "src"), "image");
            if (name == "meta") {
                auto key = lower(attribute(node, "property"));
                if (key.empty()) key = lower(attribute(node, "name"));
                if (key == "og:image" || key == "twitter:image") addUrlAsset(attribute(node, "content"), "image");
                else if (key == "og:video") addUrlAsset(attribute(node, "content"), "video");
                else if (key == "og:audio") addUrlAsset(attribute(node, "content"), "audio");
            }
            if (name == "a") {
                const auto link = resolveUrl(attribute(node, "href"), base);
                if (link) {
                    auto path = lower(pathOf(*link));
                    if (auto query = path.find('?'); query != std::string::npos) path.resize(query);
                    if (path.ends_with(".svg") || path.ends_with(".png") || path.ends_with(".jpg") || path.ends_with(".jpeg") || path.ends_with(".webp") || path.ends_with(".gif")) addUrlAsset(*link, "image");
                    else if (path.ends_with(".mp4") || path.ends_with(".mov") || path.ends_with(".webm")) addUrlAsset(*link, "video");
                    else if (path.ends_with(".mp3") || path.ends_with(".wav") || path.ends_with(".m4a") || path.ends_with(".flac") || path.ends_with(".ogg")) addUrlAsset(*link, "audio");
                    else if (path.ends_with(".csv") || path.ends_with(".tsv") || path.ends_with(".xlsx") || path.ends_with(".ods")) addUrlAsset(*link, "spreadsheet");
                    else if (path.ends_with(".gltf") || path.ends_with(".glb") || path.ends_with(".usd") || path.ends_with(".usdz") || path.ends_with(".obj")) addUrlAsset(*link, "scene");
                }
            }
            if (name == "video" || name == "audio") {
                addUrlAsset(attribute(node, "src"), name);
                if (name == "video") addUrlAsset(attribute(node, "poster"), "image");
            }
            if (name == "source" && node->parent && node->parent->name) {
                const auto owner = lower(xmlText(node->parent->name));
                if (owner == "video" || owner == "audio") addUrlAsset(attribute(node, "src"), owner);
                else if (owner == "picture") addUrlAsset(attribute(node, "src"), "image");
            }
            if (name == "svg" && (!node->parent || lower(xmlText(node->parent->name)) != "svg")) {
                auto* buffer = xmlBufferCreate();
                if (buffer) {
                    if (xmlNodeDump(buffer, doc.get(), node, 0, 0) >= 0 &&
                        xmlBufferLength(buffer) <= 4 * 1024 * 1024 - inlineBytes) {
                        Asset asset; asset.reference.kind = AssetSource::embedded; asset.reference.originatingUrl = url;
                        asset.reference.mimeType = asset.loadedMimeType = "image/svg+xml";
                        const auto* begin = reinterpret_cast<const std::uint8_t*>(xmlBufferContent(buffer));
                        asset.bytes = std::make_shared<const Bytes>(begin, begin + xmlBufferLength(buffer));
                        inlineBytes += xmlBufferLength(buffer);
                        addAsset(Value(Vector{std::move(asset)}));
                    } else out.document.assetsTruncated = true;
                    xmlBufferFree(buffer);
                }
            }
            if (name == "table") {
                Spreadsheet sheet;
                std::function<void(xmlNodePtr, int)> rows = [&](xmlNodePtr current, int level) {
                    if (level > 256) { out.document.assetsTruncated = true; return; }
                    for (; current; current = current->next) {
                        const auto tag = current->type == XML_ELEMENT_NODE ? lower(xmlText(current->name)) : std::string{};
                        if (tag == "table") continue; // Nested table is handled by the main walk.
                        if (tag == "tr") {
                            std::vector<ii::core::Value> cells;
                            for (auto* cell = current->children; cell; cell = cell->next) {
                                const auto cellName = cell->type == XML_ELEMENT_NODE ? lower(xmlText(cell->name)) : std::string{};
                                if (cellName != "td" && cellName != "th") continue;
                                if (tableCells >= 4096 || cells.size() >= 256) { out.document.assetsTruncated = true; break; }
                                auto value = content(cell);
                                if (value.size() > 65536) { value.resize(65536); out.document.assetsTruncated = true; }
                                if (ii::core::valid_utf8(value)) cells.emplace_back(std::move(value));
                                else { out.document.assetsTruncated = true; cells.emplace_back(""); }
                                ++tableCells;
                            }
                            if (sheet.rows.size() < 1024) sheet.rows.push_back(std::move(cells));
                            else out.document.assetsTruncated = true;
                        } else if (current->children) rows(current->children, level + 1);
                    }
                };
                rows(node->children, 0); addAsset(Value(std::move(sheet)));
            }
            if (node->children) walk(node->children, nextParent, textVisible, depth + 1);
        }
        if (scanned >= 100000) out.document.domTruncated = true;
    };
    walk(xmlDocGetRootElement(doc.get()), -1, true, 0);
    if (out.document.canonicalUrl.empty()) out.document.canonicalUrl = url;
    return out;
}
} // namespace iiGlobalSearch::detail
