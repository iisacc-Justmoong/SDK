#pragma once

#include <cstddef>

extern "C" {

// Borrowed byte range. The caller retains it until the synchronous call returns.
struct ii_pbr_exchange_bytes { const char* data; std::size_t size; };

// Explicitly supplied identity and bytes for one node-definition library.
struct ii_pbr_exchange_library {
    ii_pbr_exchange_bytes id, version, sha256, xml;
};

// The arrays and their contents remain caller-owned; no filesystem is accessed.
struct ii_pbr_exchange_request {
    ii_pbr_exchange_bytes original;
    const ii_pbr_exchange_library* libraries;
    std::size_t library_count;
    int request_conversion;
};

// Borrowed texture metadata and channel list for one synchronous binding call.
struct ii_pbr_texture_binding {
    ii_pbr_exchange_bytes original, sha256, uri, color_space, working_color_space, alpha;
    const ii_pbr_exchange_bytes* channels;
    std::size_t channel_count;
    ii_pbr_exchange_bytes semantic, node_name, u_address, v_address, filtering;
    const ii_pbr_exchange_library* libraries;
    std::size_t library_count;
};

// Returns an owned, NUL-terminated ASCII JSON result with byte strings encoded
// as hexadecimal, or nullptr on allocation failure. The caller must release it
// using ii_pbr_exchange_free; exceptions never cross this C ABI.
[[nodiscard]] char* ii_pbr_exchange_inspect(const ii_pbr_exchange_request* request) noexcept;
// Same ABI-owned result; verifies node library hashes and rejects ambiguous XML.
[[nodiscard]] char* ii_pbr_exchange_inspect_strict(const ii_pbr_exchange_request* request) noexcept;
// Returns the same owned JSON result, including texture metadata and original bytes.
[[nodiscard]] char* ii_pbr_exchange_convert_texture(const ii_pbr_texture_binding* request) noexcept;
// Returns an owned, lowercase 64-character hash, or nullptr on allocation failure.
[[nodiscard]] char* ii_pbr_exchange_sha256(ii_pbr_exchange_bytes bytes) noexcept;
void ii_pbr_exchange_free(char* result) noexcept;
}
