#include "MaterialExchange.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
using namespace ii::pbr::exchange;
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; ++failures; }
}

std::string read(const std::string& file)
{
    std::ifstream stream(file, std::ios::binary);
    check(stream.good(), ("Cannot read fixture: " + file).c_str());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool has(const MaterialExchangeResult& result, const std::string& code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&](const auto& diagnostic) { return diagnostic.code == code; });
}

const MaterialNode* node(const MaterialExchangeResult& result, const std::string& name)
{
    const auto found = std::find_if(result.nodes.begin(), result.nodes.end(),
        [&](const auto& item) { return item.path == name; });
    return found == result.nodes.end() ? nullptr : &*found;
}

void run()
{
    const std::string directory = IIPBR_MATERIALX_FIXTURES;
    const auto customXml = read(directory + "/custom-node.mtlx");
    const auto customLibrary = read(directory + "/custom-library.mtlx");
    const auto unknownXml = read(directory + "/unknown-node.mtlx");
    const std::string digest(64, 'a'); // This is caller-supplied metadata, not a verified digest.

    MaterialExchangeRequest request{customXml, {{"custom", "1.0", digest, customLibrary}}, false};
    const auto result = inspectMaterialX(request);
    check(result.original == customXml, "Original source bytes must be retained.");
    check(result.libraries.size() == 1 && result.libraries[0].sha256 == digest,
          "Library version and hash must be returned.");
    check(result.libraries.size() == 1 && result.libraries[0].version == "1.0",
          "Library version must be returned.");
    check(!result.converted.has_value(), "Inspection must not invent converted output.");

#if IIPBR_MATERIALX_ENABLED
    check(result.backendAvailable && result.parsed && result.nativeValid,
          "A custom node with its supplied definition should parse and validate.");
    check(has(result, "library_hash_unverified"), "Unverified caller hashes require a diagnostic.");
    check(has(result, "validation_scope_limited"), "Native validation scope requires a diagnostic.");
    const auto custom = node(result, "custom");
    check(custom && custom->requestedVersion == "1.0" && custom->resolvedVersion == "1.0",
          "Requested and resolved custom node versions must be explicit.");
    check(custom && custom->libraryId == "custom" && custom->librarySha256 == digest,
          "A resolved external node must identify its supplied library and hash.");

    const std::string standardDirectory = IIPBR_MATERIALX_STDLIB;
    const auto standardXml = read(directory + "/standard-image.mtlx");
    MaterialExchangeRequest standard{standardXml,
        {{"materialx-stdlib", "1.39.5", IIPBR_STDLIB_SHA256,
          read(standardDirectory + "/stdlib/stdlib_defs.mtlx")},
         {"materialx-bxdf", "1.39.5", IIPBR_SURFACE_SHA256,
          read(standardDirectory + "/bxdf/standard_surface.mtlx")}}, false};
    auto standardResult = inspectMaterialX(standard);
    if (!standardResult.nativeValid) {
        for (const auto& diagnostic : standardResult.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    check(standardResult.original == standardXml && standardResult.parsed && standardResult.nativeValid,
          "The reference standard surface and image graph should pass native validation.");
    const auto image = node(standardResult, "paint_color");
    check(image && image->definition == "ND_image_color3" && image->libraryId == "materialx-stdlib" &&
          image->librarySha256 == IIPBR_STDLIB_SHA256,
          "The reference image node must resolve against the declared standard library.");
    const auto surface = node(standardResult, "surface");
    check(surface && surface->requestedVersion == "1.0.1" && surface->resolvedVersion == "1.0.1" &&
          surface->libraryId == "materialx-bxdf" && surface->librarySha256 == IIPBR_SURFACE_SHA256,
          "The reference surface must retain requested/resolved versions and its library identity.");
    check(!has(standardResult, "unresolved_node"), "All nodes in the standard fixture must resolve.");

    auto wrongVersion = request;
    const auto nodePos = wrongVersion.original.find("<iisacc_example_color ");
    const auto versionPos = wrongVersion.original.find("version=\"1.0\"", nodePos);
    check(versionPos != std::string::npos, "The custom node fixture must specify its version.");
    if (versionPos != std::string::npos)
        wrongVersion.original.replace(versionPos, std::string("version=\"1.0\"").size(), "version=\"2.0\"");
    const auto wrongResult = inspectMaterialX(wrongVersion);
    check(has(wrongResult, "unresolved_node") && !wrongResult.converted,
          "A missing custom node version must be diagnosed, without conversion.");

    auto duplicateLibrary = request;
    duplicateLibrary.libraries.push_back({"other", "1.0", digest, customLibrary});
    const auto duplicateLibraryResult = inspectMaterialX(duplicateLibrary);
    check(!duplicateLibraryResult.parsed && has(duplicateLibraryResult, "duplicate_node_definition"),
          "Colliding definitions from distinct libraries must not be silently selected.");

    const std::string oldXml = "<materialx version=\"1.38\"></materialx>";
    const auto oldResult = inspectMaterialX({oldXml, {}, false});
    check(oldResult.parsed && oldResult.documentVersion == "1.38" && oldResult.original == oldXml,
          "Inspection must not silently upgrade an older document.");

    auto unknown = inspectMaterialX({unknownXml, {}, true});
    check(unknown.original == unknownXml && unknown.parsed, "Unknown source should be retained and parsed.");
    check(unknown.nativeValid, "MaterialX native validation permits this unresolved category.");
    check(has(unknown, "unresolved_node"), "An unresolved category must have a separate diagnostic.");
    check(has(unknown, "conversion_unsupported") && !unknown.converted,
          "Unsupported conversion must retain the source and report failure.");

    const auto includeXml = read(directory + "/xinclude.mtlx");
    auto include = inspectMaterialX({includeXml, {}, false});
    check(include.original == includeXml && !include.parsed && has(include, "xinclude_unsupported"),
          "External XInclude must be rejected without losing source bytes.");

    const auto duplicateXml = read(directory + "/duplicate-name.mtlx");
    auto duplicate = inspectMaterialX({duplicateXml, {}, false});
    check(duplicate.original == duplicateXml && has(duplicate, "validation_scope_limited"),
          "Potentially lossy parsing must preserve the exact original and disclose its scope.");

    auto invalid = inspectMaterialX({"<materialx><broken>", {}, false});
    check(!invalid.parsed && has(invalid, "xml_parse_failed") && invalid.original == "<materialx><broken>",
          "Malformed XML must fail explicitly while preserving the bytes.");

    auto missingVersion = request;
    missingVersion.libraries[0].version.clear();
    auto missingResult = inspectMaterialX(missingVersion);
    check(!missingResult.parsed && has(missingResult, "invalid_library_identity"),
          "Missing library identity metadata must fail explicitly.");

    auto malformedHash = request;
    malformedHash.libraries[0].sha256 = "not-a-sha256";
    const auto malformedHashResult = inspectMaterialX(malformedHash);
    check(!malformedHashResult.parsed && has(malformedHashResult, "invalid_library_identity"),
          "Malformed digest metadata must fail explicitly.");

    auto nul = request;
    nul.original.insert(0, 1, '\0');
    auto nulResult = inspectMaterialX(nul);
    check(!nulResult.parsed && nulResult.original == nul.original && has(nulResult, "invalid_input"),
          "Embedded NUL must not truncate the document silently.");
#else
    check(!result.backendAvailable && !result.parsed && has(result, "backend_unavailable"),
          "A disabled optional backend must preserve input and explain its absence.");
#endif
}
} // namespace

int main()
{
    run();
    return failures == 0 ? 0 : 1;
}
