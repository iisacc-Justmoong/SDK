#include "MaterialExchange.h"
#include "StrictMaterialXml.h"
#include "../third_party/PicoSHA2/picosha2.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>

#if defined(IIPBR_HAS_MATERIALX)
#include <MaterialXCore/Document.h>
#include <MaterialXCore/Traversal.h>
#include <MaterialXFormat/XmlIo.h>
#endif

namespace ii::pbr::exchange {
namespace {
// Append a self-contained diagnostic without retaining references to the caller.
void diagnose(MaterialExchangeResult& result, std::string code, std::string severity,
              std::string location, std::string message)
{
    result.diagnostics.push_back({std::move(code), std::move(severity),
                                  std::move(location), std::move(message)});
}

constexpr std::size_t maxDocumentBytes = 16 * 1024 * 1024;
constexpr std::size_t maxLibraryBytes = 64 * 1024 * 1024;
constexpr std::size_t maxLibraries = 128;

// Check the declared wire representation only; this does not compute a digest.
bool digestSyntax(const std::string& text)
{
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

// MaterialX's string reader is NUL-terminated, so reject rather than truncate.
bool hasNul(const std::string& text) { return text.find('\0') != std::string::npos; }

// MaterialX silently removes XInclude elements when resolution is disabled.
// A conservative lexical rejection also catches nested includes, and may reject
// a comment containing this literal; no external resolver is ever invoked.
#if defined(IIPBR_HAS_MATERIALX)
bool hasInclude(const std::string& text) { return text.find("<xi:include") != std::string::npos; }
#endif

// Enforce the in-memory limits and unambiguous supplied identities before parsing.
bool validateInput(const MaterialExchangeRequest& request, MaterialExchangeResult& result)
{
    if (request.original.empty() || request.original.size() > maxDocumentBytes || hasNul(request.original)) {
        diagnose(result, "invalid_input", "error", {}, "MaterialX document is empty, too large, or contains NUL.");
        return false;
    }
    if (request.libraries.size() > maxLibraries) {
        diagnose(result, "invalid_input", "error", {}, "Too many explicitly supplied libraries.");
        return false;
    }
    std::set<std::string> identities;
    std::size_t totalBytes = 0;
    for (const auto& library : request.libraries) {
        if (library.id.empty() || library.version.empty() || hasNul(library.id) || hasNul(library.version) ||
            !digestSyntax(library.sha256) || !identities.insert(library.id).second) {
            diagnose(result, "invalid_library_identity", "error", library.id,
                     "A unique ID, version, and 64 hexadecimal SHA-256 characters are required.");
            return false;
        }
        if (library.xml.empty() || hasNul(library.xml) || library.xml.size() > maxLibraryBytes - totalBytes) {
            diagnose(result, "invalid_input", "error", library.id, "Invalid or oversized library bytes.");
            return false;
        }
        totalBytes += library.xml.size();
    }
    return true;
}
} // namespace

std::string sha256Hex(std::string_view bytes)
{
    return picosha2::hash256_hex_string(bytes.begin(), bytes.end());
}

// One operation owns all intermediate state. Strict mode is opt-in so existing
// callers retain the documented inspection contract without silent changes.
static MaterialExchangeResult inspectImpl(const MaterialExchangeRequest& request, bool strict)
{
    MaterialExchangeResult result;
    result.original = request.original;
    for (const auto& library : request.libraries)
        result.libraries.push_back({library.id, library.version, library.sha256});

    if (request.requestConversion)
        diagnose(result, "conversion_unsupported", "error", {}, "Material conversion is not implemented.");

#if defined(IIPBR_HAS_MATERIALX)
    result.backendAvailable = true;
#endif
    if (strict) {
        if (!validateInput(request, result)) return result;
        for (const auto& library : request.libraries) {
            std::string declared = library.sha256;
            std::transform(declared.begin(), declared.end(), declared.begin(), [](unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'F' ? c + ('a' - 'A') : c);
            });
            if (sha256Hex(library.xml) != declared) {
                diagnose(result, "library_hash_mismatch", "error", library.id,
                         "The supplied SHA-256 does not match the library bytes.");
                return result;
            }
            if (const auto error = detail::checkStrictMaterialXml(library.xml)) {
                diagnose(result, error->code, "error", library.id, error->message);
                return result;
            }
        }
        if (const auto error = detail::checkStrictMaterialXml(request.original)) {
            diagnose(result, error->code, "error", {}, error->message);
            return result;
        }
    }

#if !defined(IIPBR_HAS_MATERIALX)
    diagnose(result, "backend_unavailable", "error", {}, "The optional MaterialX backend was not built.");
    return result;
#else
    if (!strict && !validateInput(request, result)) return result;
    if (hasInclude(request.original)) {
        diagnose(result, "xinclude_unsupported", "error", {}, "XInclude requires explicit host-side resolution.");
        return result;
    }

    namespace mx = MaterialX;
    mx::XmlReadOptions options;
    options.upgradeVersion = false;
    options.readComments = true;
    options.readXIncludeFunction = [](mx::DocumentPtr, const mx::FilePath&, const mx::FileSearchPath&,
                                      const mx::XmlReadOptions*) {
        throw std::invalid_argument("External XInclude access is disabled.");
    };

    auto definitions = mx::createDocument();
    std::map<std::string, std::size_t> definitionOwners;
    for (std::size_t index = 0; index < request.libraries.size(); ++index) {
        const auto& library = request.libraries[index];
        if (hasInclude(library.xml)) {
            diagnose(result, "xinclude_unsupported", "error", library.id, "Library XInclude is not resolved implicitly.");
            return result;
        }
        try {
            auto document = mx::createDocument();
            mx::readFromXmlString(document, library.xml, {}, &options);
            for (const auto& definition : document->getNodeDefs()) {
                if (!definitionOwners.emplace(definition->getName(), index).second) {
                    diagnose(result, "duplicate_node_definition", "error", definition->getName(),
                             "A node definition occurs in more than one supplied library.");
                    return result;
                }
            }
            definitions->importLibrary(document);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception& error) {
            diagnose(result, "library_parse_failed", "error", library.id, error.what());
            return result;
        }
        if (!strict)
            diagnose(result, "library_hash_unverified", "warning", library.id,
                     "The supplied SHA-256 is retained but was not checked against the library bytes.");
    }

    mx::DocumentPtr document;
    try {
        document = mx::createDocument();
        mx::readFromXmlString(document, request.original, {}, &options);
        document->setDataLibrary(definitions);
        result.parsed = true;
        result.documentVersion = document->getAttribute("version");
        std::string message;
        result.nativeValid = document->validate(&message);
        if (!result.nativeValid)
            diagnose(result, "native_validation_failed", "error", {}, message);

        for (const auto& element : document->traverseTree()) {
            const auto sourceNode = element->asA<mx::Node>();
            if (!sourceNode) continue;
            MaterialNode node{sourceNode->getNamePath(), sourceNode->getCategory(), sourceNode->getType(),
                              sourceNode->getVersionString(), {}, {}, {}, {}, {}};
            const auto definition = sourceNode->getNodeDef();
            if (!definition) {
                diagnose(result, "unresolved_node", "error", node.path,
                         "No matching node definition was found in the supplied document or libraries.");
            } else {
                node.definition = definition->getName();
                node.resolvedVersion = definition->getVersionString();
                const auto owner = definitionOwners.find(node.definition);
                // getChild() also searches the data library, so ownership must
                // be determined from the resolved definition's document itself.
                if (owner != definitionOwners.end() && definition->getDocument() == definitions) {
                    const auto& library = request.libraries[owner->second];
                    node.libraryId = library.id;
                    node.libraryVersion = library.version;
                    node.librarySha256 = library.sha256;
                }
            }
            result.nodes.push_back(std::move(node));
        }
        diagnose(result, "validation_scope_limited", "warning", {},
                 "MaterialX validation does not prove complete parsing, conversion, or renderer support; retain original bytes.");
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        diagnose(result, result.parsed ? "backend_failure" : "xml_parse_failed", "error", {}, error.what());
    }
    return result;
#endif
}

MaterialExchangeResult inspectMaterialX(const MaterialExchangeRequest& request)
{
    return inspectImpl(request, false);
}

MaterialExchangeResult inspectMaterialXStrict(const MaterialExchangeRequest& request)
{
    return inspectImpl(request, true);
}
} // namespace ii::pbr::exchange
