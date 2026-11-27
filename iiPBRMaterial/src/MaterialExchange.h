#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::pbr::exchange {

// A caller-owned MaterialX definition document and its declared identity.
// The bytes are copied into the request; the adapter never opens its own files.
// sha256 is caller-supplied: legacy inspection retains it without checking;
// strict inspection and the texture adapter verify it against the XML bytes.
struct MaterialLibrary {
    std::string id;
    std::string version;
    std::string sha256;
    std::string xml;
};

// A durable identity returned independently of the transient library bytes.
struct MaterialLibraryIdentity {
    std::string id;
    std::string version;
    std::string sha256;
};

// A resolved node is an inspection result, not a promise of renderer support.
// An empty library identity means that no external supplied definition resolved.
struct MaterialNode {
    std::string path;
    std::string category;
    std::string type;
    std::string requestedVersion;
    std::string definition;
    std::string resolvedVersion;
    std::string libraryId;
    std::string libraryVersion;
    std::string librarySha256;
};

// Stable machine-readable code plus explanatory text and optional element path.
// severity is "error" or "warning"; diagnostic strings are owned by the result.
struct MaterialDiagnostic {
    std::string code;
    std::string severity;
    std::string location;
    std::string message;
};

// All authority needed for one inspection is explicit. If requestConversion is
// true, this first slice reports that conversion is unsupported rather than
// manufacturing a partial surface or discarding unknown nodes.
struct MaterialExchangeRequest {
    std::string original;
    std::vector<MaterialLibrary> libraries;
    bool requestConversion = false;
};

// Explicit texture meaning is retained even when a conversion fails. The
// caller controls the URI and resource lifetime; this structure performs no I/O.
struct TextureMetadata {
    std::string sha256;
    std::string uri;
    std::string colorSpace;
    std::string workingColorSpace;
    std::string alpha;
    std::vector<std::string> channels;
    std::string semantic;
    std::string nodeName;
    std::string uAddress;
    std::string vAddress;
    std::string filtering;
};

// The result owns the original bytes even on ordinary parse or validation errors.
// nativeValid reports only MaterialX's own validation, not renderer or conversion
// compatibility. No method exposes an internal MaterialX object or borrowed data.
struct MaterialExchangeResult {
    std::string original;
    std::vector<MaterialLibraryIdentity> libraries;
    bool backendAvailable = false;
    bool parsed = false;
    bool nativeValid = false;
    std::string documentVersion;
    std::vector<MaterialNode> nodes;
    std::vector<MaterialDiagnostic> diagnostics;
    std::optional<std::string> converted;
    std::optional<TextureMetadata> texture;
};

// Inspect supplied bytes without I/O, globals, automatic upgrades or XInclude
// resolution. Each call owns its result. Callers must serialize concurrent use
// until the chosen MaterialX build has an independently verified thread contract.
// Resource exhaustion may still throw std::bad_alloc; ordinary input and backend
// errors are represented by diagnostics alongside the preserved original.
[[nodiscard]] MaterialExchangeResult inspectMaterialX(const MaterialExchangeRequest& request);

// Strict inspection verifies each declared library hash and conservatively
// rejects duplicate names or XML forms it cannot inspect before MaterialX runs.
// The input is always retained; unsupported XML is an error, not a rewrite.
[[nodiscard]] MaterialExchangeResult inspectMaterialXStrict(const MaterialExchangeRequest& request);

// Compute the SHA-256 of exactly the supplied bytes, including embedded NULs.
// The returned lowercase hexadecimal value owns its storage and has no I/O.
[[nodiscard]] std::string sha256Hex(std::string_view bytes);

} // namespace ii::pbr::exchange
