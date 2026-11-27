#include "MaterialExchangeC.h"
#include "MaterialExchange.h"
#include "TextureBinding.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace ii::pbr::exchange;

// Copy caller-owned bytes immediately; reject a null pointer with a nonzero size.
std::string copy(ii_pbr_exchange_bytes bytes)
{
    if (!bytes.data && bytes.size) throw std::invalid_argument("Null byte range.");
    return bytes.size ? std::string(bytes.data, bytes.size) : std::string{};
}

// The C ABI transports arbitrary source bytes as ASCII hexadecimal in JSON.
std::string hex(std::string_view input)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(input.size() * 2);
    for (const unsigned char byte : input) {
        output.push_back(digits[byte >> 4]);
        output.push_back(digits[byte & 15]);
    }
    return output;
}

// Field names are fixed internal ASCII; values are encoded without loss.
void field(std::string& json, std::string_view name, std::string_view value)
{
    json += '"'; json += name; json += "\":\""; json += hex(value); json += '"';
}

// Serialize an owned value using the explicitly versioned binding protocol.
std::string serialize(const MaterialExchangeResult& result)
{
    std::string json = "{\"schema\":1,";
    field(json, "original", result.original);
    json += ",\"backend_available\":"; json += result.backendAvailable ? "true" : "false";
    json += ",\"parsed\":"; json += result.parsed ? "true" : "false";
    json += ",\"native_valid\":"; json += result.nativeValid ? "true" : "false";
    json += ','; field(json, "document_version", result.documentVersion);
    json += ",\"converted\":";
    if (result.converted) { json += '"'; json += hex(*result.converted); json += '"'; }
    else json += "null";
    json += ",\"libraries\":[";
    for (std::size_t i = 0; i < result.libraries.size(); ++i) {
        if (i) json += ',';
        json += '{'; field(json, "id", result.libraries[i].id); json += ',';
        field(json, "version", result.libraries[i].version); json += ',';
        field(json, "sha256", result.libraries[i].sha256); json += '}';
    }
    json += "],\"nodes\":[";
    for (std::size_t i = 0; i < result.nodes.size(); ++i) {
        if (i) json += ',';
        const auto& node = result.nodes[i];
        json += '{'; field(json, "path", node.path); json += ',';
        field(json, "category", node.category); json += ','; field(json, "type", node.type); json += ',';
        field(json, "requested_version", node.requestedVersion); json += ',';
        field(json, "definition", node.definition); json += ',';
        field(json, "resolved_version", node.resolvedVersion); json += ',';
        field(json, "library_id", node.libraryId); json += ',';
        field(json, "library_version", node.libraryVersion); json += ',';
        field(json, "library_sha256", node.librarySha256); json += '}';
    }
    json += "],\"diagnostics\":[";
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
        if (i) json += ',';
        const auto& diagnostic = result.diagnostics[i];
        json += '{'; field(json, "code", diagnostic.code); json += ',';
        field(json, "severity", diagnostic.severity); json += ',';
        field(json, "location", diagnostic.location); json += ',';
        field(json, "message", diagnostic.message); json += '}';
    }
    json += "],\"texture\":";
    if (!result.texture) {
        json += "null";
    } else {
        const auto& texture = *result.texture;
        json += '{'; field(json, "sha256", texture.sha256); json += ',';
        field(json, "uri", texture.uri); json += ','; field(json, "color_space", texture.colorSpace); json += ',';
        field(json, "working_color_space", texture.workingColorSpace); json += ',';
        field(json, "alpha", texture.alpha); json += ",\"channels\":[";
        for (std::size_t i = 0; i < texture.channels.size(); ++i) {
            if (i) json += ',';
            json += '"'; json += hex(texture.channels[i]); json += '"';
        }
        json += "],"; field(json, "semantic", texture.semantic); json += ',';
        field(json, "node_name", texture.nodeName); json += ',';
        field(json, "u_address", texture.uAddress); json += ',';
        field(json, "v_address", texture.vAddress); json += ',';
        field(json, "filtering", texture.filtering); json += '}';
    }
    json += '}';
    return json;
}

// Copy the explicitly supplied library array and bound its count before access.
std::vector<MaterialLibrary> copyLibraries(const ii_pbr_exchange_library* values, std::size_t count)
{
    if ((!values && count) || count > 128) throw std::invalid_argument("Invalid library array.");
    std::vector<MaterialLibrary> result;
    for (std::size_t i = 0; i < count; ++i)
        result.push_back({copy(values[i].id), copy(values[i].version), copy(values[i].sha256), copy(values[i].xml)});
    return result;
}

// Allocate a C-owned result; callers release it through the matching ABI.
char* allocate(std::string_view value)
{
    auto* output = static_cast<char*>(std::malloc(value.size() + 1));
    if (!output) return nullptr;
    std::memcpy(output, value.data(), value.size());
    output[value.size()] = '\0';
    return output;
}

char* invalid(const std::invalid_argument& error)
{
    MaterialExchangeResult result;
    result.diagnostics.push_back({"invalid_input", "error", {}, error.what()});
    return allocate(serialize(result));
}

char* inspect(const ii_pbr_exchange_request* request, bool strict)
{
    if (!request) throw std::invalid_argument("Invalid exchange request.");
    MaterialExchangeRequest input;
    input.original = copy(request->original);
    input.requestConversion = request->request_conversion != 0;
    input.libraries = copyLibraries(request->libraries, request->library_count);
    return allocate(serialize(strict ? inspectMaterialXStrict(input) : inspectMaterialX(input)));
}
} // namespace

extern "C" char* ii_pbr_exchange_inspect(const ii_pbr_exchange_request* request) noexcept
{
    try {
        return inspect(request, false);
    } catch (const std::invalid_argument& error) {
        try { return invalid(error); } catch (...) { return nullptr; }
    } catch (...) { return nullptr; }
}

extern "C" char* ii_pbr_exchange_inspect_strict(const ii_pbr_exchange_request* request) noexcept
{
    try { return inspect(request, true); }
    catch (const std::invalid_argument& error) { try { return invalid(error); } catch (...) { return nullptr; } }
    catch (...) { return nullptr; }
}

extern "C" char* ii_pbr_exchange_convert_texture(const ii_pbr_texture_binding* request) noexcept
{
    try {
        if (!request || (!request->channels && request->channel_count) || request->channel_count > 16)
            throw std::invalid_argument("Invalid texture binding request.");
        TextureBindingRequest input;
        input.original = copy(request->original);
        auto& meta = input.metadata;
        meta.sha256 = copy(request->sha256); meta.uri = copy(request->uri);
        meta.colorSpace = copy(request->color_space); meta.workingColorSpace = copy(request->working_color_space);
        meta.alpha = copy(request->alpha);
        for (std::size_t i = 0; i < request->channel_count; ++i) meta.channels.push_back(copy(request->channels[i]));
        meta.semantic = copy(request->semantic); meta.nodeName = copy(request->node_name);
        meta.uAddress = copy(request->u_address); meta.vAddress = copy(request->v_address);
        meta.filtering = copy(request->filtering);
        input.libraries = copyLibraries(request->libraries, request->library_count);
        return allocate(serialize(convertTextureToMaterialX(input)));
    } catch (const std::invalid_argument& error) { try { return invalid(error); } catch (...) { return nullptr; } }
    catch (...) { return nullptr; }
}

extern "C" char* ii_pbr_exchange_sha256(ii_pbr_exchange_bytes bytes) noexcept
{
    try { return allocate(sha256Hex(copy(bytes))); }
    catch (...) { return nullptr; }
}

extern "C" void ii_pbr_exchange_free(char* result) noexcept { std::free(result); }
