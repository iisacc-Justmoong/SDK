#include <iiCompression/Gzip.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
int failures = 0;
void expect(bool value, std::string_view message)
{
    if (!value) { ++failures; std::cerr << message << '\n'; }
}
}

int main()
{
    using namespace ii::compression;
    const Bytes known{0x1f,0x8b,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0xff,0xcb,0x48,0xcd,0xc9,
                      0xc9,0x57,0x48,0xaf,0xca,0x2c,0x00,0x00,0x19,0x6a,0xd2,0xdf,0x0a,0x00,0x00,0x00};
    const Bytes plain{'h','e','l','l','o',' ','g','z','i','p'};
    const auto decoded = gzip_decompress(known);
    expect(decoded && *decoded == plain, "decode an independently generated gzip member");
    const auto encoded = gzip_compress(plain);
    expect(encoded && encoded->size() > 2 && (*encoded)[0] == 0x1f && (*encoded)[1] == 0x8b,
           "produce a gzip stream");
    if (encoded) {
        const auto roundTrip = gzip_decompress(*encoded);
        expect(roundTrip && *roundTrip == plain, "round trip binary bytes");
    }
    const Bytes empty;
    const auto emptyGzip = gzip_compress(empty);
    if (emptyGzip) {
        const auto roundTrip = gzip_decompress(*emptyGzip);
        expect(roundTrip && *roundTrip == empty, "round trip an empty member");
        Limits emptyLimit; emptyLimit.output_bytes = 0;
        const auto limitedEmpty = gzip_decompress(*emptyGzip, emptyLimit);
        expect(limitedEmpty && limitedEmpty->empty(), "an empty member fits a zero-byte output limit");
    } else expect(false, "encode an empty member");

    Bytes binary(100000);
    std::uint32_t state = 0x12345678U;
    for (auto &byte : binary) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        byte = static_cast<std::uint8_t>(state);
    }
    const auto largeGzip = gzip_compress(binary);
    expect(largeGzip && largeGzip->size() > 16384, "exercise multiple compressed output blocks");
    if (largeGzip) {
        const auto roundTrip = gzip_decompress(*largeGzip);
        expect(roundTrip && *roundTrip == binary, "round trip multi-block binary data");
        Limits exact; exact.output_bytes = binary.size();
        const auto fits = gzip_decompress(*largeGzip, exact);
        expect(fits && *fits == binary, "accept an exact expanded byte limit");
        --exact.output_bytes;
        const auto exceeds = gzip_decompress(*largeGzip, exact);
        expect(!exceeds && exceeds.error().code == ErrorCode::limit_exceeded,
               "reject multi-block expansion beyond the byte limit");
    }

    auto truncated = known; truncated.pop_back();
    expect(!gzip_decompress(truncated), "reject a truncated gzip trailer");
    expect(!gzip_decompress(empty), "reject empty encoded input");
    expect(!gzip_decompress(Bytes{1, 2, 3, 4}), "reject data without a gzip header");
    auto damaged = known; damaged[damaged.size() - 8] ^= 1;
    expect(!gzip_decompress(damaged), "reject a checksum mismatch");
    auto trailing = known; trailing.push_back(0);
    expect(!gzip_decompress(trailing), "reject trailing bytes");
    auto concatenated = known; concatenated.insert(concatenated.end(), known.begin(), known.end());
    expect(!gzip_decompress(concatenated), "reject concatenated gzip members");
    Limits limits; limits.input_bytes = 4;
    auto inputLimited = gzip_decompress(known, limits);
    expect(!inputLimited && inputLimited.error().code == ErrorCode::limit_exceeded, "bound input bytes");
    auto compressInputLimited = gzip_compress(plain, limits);
    expect(!compressInputLimited && compressInputLimited.error().code == ErrorCode::limit_exceeded,
           "bound compression input bytes");
    limits = {}; limits.output_bytes = plain.size() - 1;
    auto outputLimited = gzip_decompress(known, limits);
    expect(!outputLimited && outputLimited.error().code == ErrorCode::limit_exceeded, "bound expanded bytes");
    limits = {}; limits.output_bytes = 1;
    auto compressedLimited = gzip_compress(plain, limits);
    expect(!compressedLimited && compressedLimited.error().code == ErrorCode::limit_exceeded,
           "bound compressed output bytes");
    return failures ? 1 : 0;
}
