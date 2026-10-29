#include <iiCompression/Gzip.h>

#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    const std::vector<std::uint8_t> input{0, 1, 2, 3, 0, 255, 'g', 'z', 'i', 'p'};
    const auto encoded = ii::compression::gzip_compress(input);
    if (!encoded) return 1;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : *encoded) std::cout << hex[byte >> 4] << hex[byte & 15];
    std::cout << '\n';
}
