#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace ii::compression {
enum class ErrorCode { invalid_data, limit_exceeded, codec_failure };
struct Error { ErrorCode code; std::string message; };
struct Limits {
    std::uint64_t input_bytes = 256ULL * 1024 * 1024;
    std::uint64_t output_bytes = 512ULL * 1024 * 1024;
};
using Bytes = std::vector<std::uint8_t>;
using Result = std::expected<Bytes, Error>;

// Bounded memory-only operations. Decode accepts exactly one complete gzip
// member and returns no partial output on corruption, trailing data or limits.
[[nodiscard]] Result gzip_compress(std::span<const std::uint8_t> input, Limits limits = {});
[[nodiscard]] Result gzip_decompress(std::span<const std::uint8_t> input, Limits limits = {});
} // namespace ii::compression
