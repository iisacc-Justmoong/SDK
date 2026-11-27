#pragma once
#include <cstddef>
#include <cstdint>

// Private C ABI between the command validator and the precision backend. The
// caller retains every span, resource, and callback until the call returns;
// the backend neither stores them nor acquires filesystem or network access.
extern "C" {
struct iiagent_precision_slice { const char* data; std::size_t size; };
struct iiagent_precision_resource { iiagent_precision_slice uri, json; };
using iiagent_precision_format = int (*)(void*, std::uint32_t, iiagent_precision_slice);
struct iiagent_precision_request {
    iiagent_precision_slice schema, instance;
    const iiagent_precision_resource* resources;
    std::size_t resource_count;
    std::uint32_t draft;
    bool assert_formats;
    iiagent_precision_format format;
    void* context;
};
// Returns 0 for valid, 1 for invalid, and 2 for malformed or unsupported input.
// diagnostic is optional caller-owned writable storage. Independent calls are
// thread safe; exceptions and panics never cross this ABI boundary.
int iiagent_precision_validate(const iiagent_precision_request*, char* diagnostic, std::size_t diagnostic_size) noexcept;
}
