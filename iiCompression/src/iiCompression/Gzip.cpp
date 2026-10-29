#include "Gzip.h"

#include <zlib.h>

#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>

namespace ii::compression {
namespace {
Result failure(ErrorCode code, const char *message)
{
    return std::unexpected(Error{code, message});
}
bool exceedsInput(std::span<const std::uint8_t> input, Limits limits)
{
    return input.size() > limits.input_bytes || input.size() > std::numeric_limits<uInt>::max();
}
struct Encoder {
    z_stream stream{};
    bool initialized = false;
    ~Encoder() { if (initialized) deflateEnd(&stream); }
};
struct Decoder {
    z_stream stream{};
    bool initialized = false;
    ~Decoder() { if (initialized) inflateEnd(&stream); }
};
bool append(Bytes &output, const std::array<std::uint8_t, 16384> &block,
            std::size_t count, std::uint64_t limit)
{
    if (output.size() > limit || count > limit - output.size()
        || count > output.max_size() - output.size()) return false;
    output.insert(output.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
    return true;
}
} // namespace

Result gzip_compress(std::span<const std::uint8_t> input, Limits limits)
{
    try {
        if (exceedsInput(input, limits)) return failure(ErrorCode::limit_exceeded, "gzip input exceeds its byte limit");
        Encoder encoder;
        auto &stream = encoder.stream;
        if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return failure(ErrorCode::codec_failure, "cannot initialize gzip compressor");
        encoder.initialized = true;
        stream.next_in = input.empty() ? Z_NULL : const_cast<Bytef *>(input.data());
        stream.avail_in = static_cast<uInt>(input.size());
        Bytes output;
        std::array<std::uint8_t, 16384> block{};
        for (;;) {
            stream.next_out = block.data();
            stream.avail_out = static_cast<uInt>(block.size());
            const auto before = stream.avail_in;
            const int status = deflate(&stream, Z_FINISH);
            const auto count = block.size() - stream.avail_out;
            if (!append(output, block, count, limits.output_bytes))
                return failure(ErrorCode::limit_exceeded, "gzip output exceeds its byte limit");
            if (status == Z_STREAM_END && stream.avail_in == 0) return output;
            if (status != Z_OK || (count == 0 && stream.avail_in == before))
                return failure(ErrorCode::codec_failure, "cannot finish gzip stream");
        }
    } catch (const std::bad_alloc &) {
        return failure(ErrorCode::limit_exceeded, "gzip allocation failed");
    } catch (const std::length_error &) {
        return failure(ErrorCode::limit_exceeded, "gzip output is too large");
    }
}

Result gzip_decompress(std::span<const std::uint8_t> input, Limits limits)
{
    try {
        if (exceedsInput(input, limits)) return failure(ErrorCode::limit_exceeded, "gzip input exceeds its byte limit");
        if (input.empty()) return failure(ErrorCode::invalid_data, "gzip input is empty");
        Decoder decoder;
        auto &stream = decoder.stream;
        if (inflateInit2(&stream, 15 + 16) != Z_OK)
            return failure(ErrorCode::codec_failure, "cannot initialize gzip decoder");
        decoder.initialized = true;
        stream.next_in = const_cast<Bytef *>(input.data());
        stream.avail_in = static_cast<uInt>(input.size());
        Bytes output;
        std::array<std::uint8_t, 16384> block{};
        for (;;) {
            stream.next_out = block.data();
            stream.avail_out = static_cast<uInt>(block.size());
            const auto before = stream.avail_in;
            const int status = inflate(&stream, Z_NO_FLUSH);
            const auto count = block.size() - stream.avail_out;
            if (!append(output, block, count, limits.output_bytes))
                return failure(ErrorCode::limit_exceeded, "expanded gzip output exceeds its byte limit");
            if (status == Z_STREAM_END) {
                if (stream.avail_in != 0)
                    return failure(ErrorCode::invalid_data, "gzip input has trailing data or multiple members");
                return output;
            }
            if (status == Z_MEM_ERROR) return failure(ErrorCode::limit_exceeded, "gzip decoder allocation failed");
            if (status == Z_DATA_ERROR || status == Z_NEED_DICT || status == Z_BUF_ERROR
                || (status == Z_OK && count == 0 && stream.avail_in == before))
                return failure(ErrorCode::invalid_data, "invalid, truncated or corrupt gzip input");
            if (status != Z_OK) return failure(ErrorCode::codec_failure, "gzip decoder failed");
        }
    } catch (const std::bad_alloc &) {
        return failure(ErrorCode::limit_exceeded, "gzip allocation failed");
    } catch (const std::length_error &) {
        return failure(ErrorCode::limit_exceeded, "expanded gzip output is too large");
    }
}
} // namespace ii::compression
