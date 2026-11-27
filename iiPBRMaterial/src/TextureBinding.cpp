#include "TextureBinding.h"

#include <algorithm>
#include <cstdint>
#include <exception>

#if defined(IIPBR_HAS_MATERIALX)
#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>
#endif

namespace ii::pbr::exchange {
namespace {
// Diagnose without modifying caller-owned texture bytes or metadata.
void failure(MaterialExchangeResult& result, std::string code, std::string message)
{
    result.diagnostics.push_back({std::move(code), "error", {}, std::move(message)});
}

bool asciiLetter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool asciiDigit(unsigned char c) { return c >= '0' && c <= '9'; }

// MaterialX names are deliberately restricted to an unambiguous ASCII subset.
bool validName(std::string_view value)
{
    if (value.empty() || !(asciiLetter(value.front()) || value.front() == '_')) return false;
    return std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
        return asciiLetter(c) || asciiDigit(c) || c == '_';
    });
}

// Reject malformed UTF-8 and XML-forbidden scalar values before serialization.
bool validXmlText(std::string_view value)
{
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i]);
        std::uint32_t code = 0;
        std::size_t count = 0;
        if (first < 0x80) { code = first; count = 1; }
        else if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1f; count = 2; }
        else if (first >= 0xe0 && first <= 0xef) { code = first & 0x0f; count = 3; }
        else if (first >= 0xf0 && first <= 0xf4) { code = first & 0x07; count = 4; }
        else return false;
        if (i + count > value.size()) return false;
        for (std::size_t j = 1; j < count; ++j) {
            const auto next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800) ||
            (count == 4 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff) || code == 0xfffe || code == 0xffff || code < 0x20) return false;
        i += count;
    }
    return true;
}

bool digestMatches(std::string_view bytes, std::string expected)
{
    if (expected.size() != 64) return false;
    for (char& c : expected) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c + ('a' - 'A'));
        if (!asciiDigit(c) && !(c >= 'a' && c <= 'f')) return false;
    }
    return sha256Hex(bytes) == expected;
}

bool addressMode(std::string_view value)
{
    return value == "constant" || value == "clamp" || value == "periodic" || value == "mirror";
}

bool filterMode(std::string_view value)
{
    return value == "closest" || value == "linear" || value == "cubic";
}

// Only framing and IHDR channel declarations are inspected: IDAT, CRCs, color
// profiles, and actual pixels remain unverified and are always reported as such.
struct PngInfo { bool recognized = false, framed = false, transparency = false; unsigned depth = 0, type = 0; };

PngInfo inspectPng(std::string_view bytes)
{
    constexpr std::string_view signature("\x89PNG\r\n\x1a\n", 8);
    PngInfo info;
    if (bytes.size() < 8 || bytes.substr(0, 8) != signature) return info;
    info.recognized = true;
    if (bytes.size() < 33 || bytes.substr(8, 4) != std::string_view("\0\0\0\x0d", 4) ||
        bytes.substr(12, 4) != "IHDR") return info;
    const auto nonzero = [&](std::size_t offset) {
        return bytes[offset] || bytes[offset + 1] || bytes[offset + 2] || bytes[offset + 3];
    };
    if (!nonzero(16) || !nonzero(20) || bytes[26] != 0 || bytes[27] != 0 || (bytes[28] != 0 && bytes[28] != 1))
        return info;
    info.depth = static_cast<unsigned char>(bytes[24]);
    info.type = static_cast<unsigned char>(bytes[25]);
    bool foundImage = false;
    for (std::size_t pos = 8; pos <= bytes.size() - 12;) {
        const auto byte = [&](std::size_t index) { return static_cast<unsigned char>(bytes[index]); };
        const auto size = (std::uint32_t(byte(pos)) << 24) | (std::uint32_t(byte(pos + 1)) << 16) |
                          (std::uint32_t(byte(pos + 2)) << 8) | std::uint32_t(byte(pos + 3));
        if (size > bytes.size() - pos - 12) return info;
        const auto chunk = bytes.substr(pos + 4, 4);
        if (pos != 8 && chunk == "IHDR") return info;
        if (chunk == "tRNS") info.transparency = true;
        if (chunk == "IDAT") foundImage = true;
        pos += static_cast<std::size_t>(size) + 12;
        if (chunk == "IEND") {
            info.framed = size == 0 && foundImage && pos == bytes.size();
            break;
        }
    }
    return info;
}
} // namespace

MaterialExchangeResult convertTextureToMaterialX(const TextureBindingRequest& request)
{
    MaterialExchangeResult result;
    result.original = request.original;
    result.texture = request.metadata;
    for (const auto& library : request.libraries)
        result.libraries.push_back({library.id, library.version, library.sha256});
#if defined(IIPBR_HAS_MATERIALX)
    result.backendAvailable = true;
#endif

    const auto& meta = request.metadata;
    if (request.original.empty() || request.original.size() > 64 * 1024 * 1024 ||
        meta.uri.empty() || meta.colorSpace.empty() || meta.workingColorSpace.empty() ||
        !validName(meta.nodeName) || !validXmlText(meta.uri) || !validXmlText(meta.colorSpace) ||
        !validXmlText(meta.workingColorSpace) || !addressMode(meta.uAddress) ||
        !addressMode(meta.vAddress) || !filterMode(meta.filtering)) {
        failure(result, "invalid_texture_metadata", "Texture bytes and explicit valid binding metadata are required.");
        return result;
    }
    const std::vector<std::string> rgb{"red", "green", "blue"};
    const std::vector<std::string> rgba{"red", "green", "blue", "alpha"};
    // A recognized PNG must have unambiguous framing and matching channel
    // declarations; other formats remain caller-described and unverified.
    const auto png = inspectPng(request.original);
    const bool opaquePng = png.framed && png.depth == 8 && png.type == 2 && !png.transparency;
    const bool rgbaPng = png.framed && png.depth == 8 && png.type == 6 && !png.transparency;
    const bool opaqueRgb = meta.alpha == "none" && meta.channels == rgb && (!png.recognized || opaquePng);
    const bool straightRgba = meta.alpha == "straight" && meta.channels == rgba && rgbaPng;
    if ((!opaqueRgb && !straightRgba) || meta.semantic != "base_color") {
        failure(result, "unsupported_texture_layout", "Expected opaque RGB or straight RGBA8 PNG base-color metadata.");
        return result;
    }
    if (!digestMatches(request.original, meta.sha256)) {
        failure(result, "texture_hash_mismatch", "The texture SHA-256 does not match the supplied bytes.");
        return result;
    }

#if !defined(IIPBR_HAS_MATERIALX)
    failure(result, "backend_unavailable", "The optional MaterialX backend was not built.");
    return result;
#else
    namespace mx = MaterialX;
    try {
        auto document = mx::createDocument();
        document->setAttribute("version", "1.39");
        document->setAttribute("colorspace", meta.workingColorSpace);
        auto image = document->addNode("image", meta.nodeName, straightRgba ? "color4" : "color3");
        image->setNodeDefString(straightRgba ? "ND_image_color4" : "ND_image_color3");
        auto file = image->addInput("file", "filename");
        file->setValueString(meta.uri);
        file->setAttribute("colorspace", meta.colorSpace);
        image->addInput("uaddressmode", "string")->setValueString(meta.uAddress);
        image->addInput("vaddressmode", "string")->setValueString(meta.vAddress);
        image->addInput("filtertype", "string")->setValueString(meta.filtering);
        mx::XmlWriteOptions options;
        options.writeXIncludeEnable = false;
        const auto xml = mx::writeToXmlString(document, &options);

        // Strict inspection checks library hashes, duplicate names, validation,
        // and resolution before publishing the newly generated XML value.
        auto inspected = inspectMaterialXStrict({xml, request.libraries, false});
        result.libraries = std::move(inspected.libraries);
        result.parsed = inspected.parsed;
        result.nativeValid = inspected.nativeValid;
        result.documentVersion = std::move(inspected.documentVersion);
        result.nodes = std::move(inspected.nodes);
        result.diagnostics = std::move(inspected.diagnostics);
        const auto error = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                                       [](const auto& diagnostic) { return diagnostic.severity == "error"; });
        if (!result.parsed || !result.nativeValid || error) return result;
        result.converted = xml;
        result.diagnostics.push_back({"texture_content_unverified", "warning", {},
            "The digest was verified, but image encoding and declared channels were not decoded."});
        result.diagnostics.push_back({"uv_binding_required", "warning", meta.nodeName,
            "The host must connect the image node to a material and choose the appropriate UV coordinates."});
        if (straightRgba)
            result.diagnostics.push_back({"alpha_association_external", "warning", meta.nodeName,
                "The host must preserve straight-alpha metadata when connecting or storing this image node."});
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        failure(result, "backend_failure", error.what());
    }
    return result;
#endif
}

MaterialXTextureConverter::MaterialXTextureConverter(std::vector<MaterialLibrary> libraries)
    : libraries_(std::move(libraries))
{
}

MaterialExchangeResult MaterialXTextureConverter::execute(const TextureConversionInput& input) const
{
    return convertTextureToMaterialX({input.original, input.metadata, libraries_});
}
} // namespace ii::pbr::exchange
