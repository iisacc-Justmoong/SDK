#include <iiGraphicAdjust/SampleFilters.h>
#include <photoshop_reverse/Filters.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace ga = iiGraphicAdjust;
namespace ref = photoshop_reverse;
namespace {
std::uint64_t cases = 0, checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class T> bool equalSample(T a, T b) {
    if constexpr (std::is_same_v<T, float>)
        return (std::isnan(a) && std::isnan(b)) ||
               std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    else return a == b;
}
template<class T> void compareAverage(std::uint32_t w, std::uint32_t h, bool selected) {
    const std::size_t stride = w + 3, maskStride = w + 7;
    std::vector<T> input(stride * h, T(17));
    std::vector<std::uint8_t> mask(maskStride * h, 0);
    for (std::uint32_t y = 0; y < h; ++y) for (std::uint32_t x = 0; x < w; ++x) {
        const auto index = y * stride + x;
        if constexpr (std::is_same_v<T, float>) {
            constexpr float values[]{1e8F, 1.F, -1e8F, .25F, -.5F, 3.F, -0.F};
            input[index] = values[(x + 3 * y) % 7];
        } else input[index] = T((x * 177 + y * 103) % (std::is_same_v<T, std::uint8_t> ? 256 : 65536));
        constexpr std::uint8_t selections[]{0, 1, 127, 128, 254, 255};
        mask[y * maskStride + x] = selections[(x + y) % 6];
    }
    const ga::SamplePlaneView<T> sdkInput{input, w, h, stride};
    const ref::PlaneView<T> referenceInput{input, w, h, stride};
    const auto sdkMask = selected ? std::optional<ga::SampleSelectionMask>{{mask, w, h, maskStride}}
                                 : std::nullopt;
    const auto referenceMask = selected ? std::optional<ref::SelectionMask>{{mask, w, h, maskStride}}
                                       : std::nullopt;
    std::vector<T> sdkOutput(input.size(), T(29)), referenceOutput = sdkOutput;
    auto sdk = [&] {
        if constexpr (std::is_same_v<T, std::uint8_t>)
            return ga::fillSampleAverage8(sdkInput, {sdkOutput, w, h, stride}, sdkMask);
        else if constexpr (std::is_same_v<T, std::uint16_t>)
            return ga::fillSampleAverage16(sdkInput, {sdkOutput, w, h, stride}, sdkMask);
        else return ga::fillSampleAverageFloat32(sdkInput, {sdkOutput, w, h, stride}, sdkMask);
    }();
    auto original = [&] {
        if constexpr (std::is_same_v<T, std::uint8_t>)
            return ref::averageFill8(referenceInput, {referenceOutput, w, h, stride}, referenceMask);
        else if constexpr (std::is_same_v<T, std::uint16_t>)
            return ref::averageFill16(referenceInput, {referenceOutput, w, h, stride}, referenceMask);
        else return ref::averageFillFloat32(referenceInput, {referenceOutput, w, h, stride}, referenceMask);
    }();
    ++cases;
    check(sdk && original, "average comparison call failed");
    check(equalSample(sdk->value, original->value) && sdk->selectedSamples == original->selectedSamples
           && sdk->tiles == original->tiles, "average result differs from pinned reconstruction");
    for (std::size_t i = 0; i < sdkOutput.size(); ++i)
        check(equalSample(sdkOutput[i], referenceOutput[i]), "average fill or padding differs");
}
template<class T> void compareSolarize(unsigned domain) {
    constexpr std::uint32_t width = 256;
    const auto height = std::uint32_t(domain / width);
    constexpr std::size_t stride = 259;
    std::vector<T> sdk(stride * height, T(19));
    for (unsigned i = 0; i < domain; ++i) sdk[(i / width) * stride + i % width] = T(i);
    auto original = sdk;
    if constexpr (std::is_same_v<T, std::uint8_t>) {
        check(bool(ga::solarizeSamples8({sdk, width, height, stride})), "sdk byte solarize failed");
        check(bool(ref::solarize8({original, width, height, stride})), "reference byte solarize failed");
    } else {
        check(bool(ga::solarizeSamples16({sdk, width, height, stride})), "sdk word solarize failed");
        check(bool(ref::solarize16({original, width, height, stride})), "reference word solarize failed");
    }
    ++cases;
    for (std::size_t i = 0; i < sdk.size(); ++i) check(sdk[i] == original[i], "solarize differs from reference");
}
} // namespace
int main() {
    try {
        for (auto w : {1U, 7U, 8U, 255U, 256U, 257U, 513U})
            for (auto h : {1U, 2U, 256U, 257U}) for (bool mask : {false, true}) {
                compareAverage<std::uint8_t>(w, h, mask);
                compareAverage<std::uint16_t>(w, h, mask);
                compareAverage<float>(w, h, mask);
            }
        compareSolarize<std::uint8_t>(256);
        compareSolarize<std::uint16_t>(65536);
        std::cout << "{\"authority\":\"pinned-cpp-reconstruction-comparison\",\"cases\":" << cases
                  << ",\"checks\":" << checks << ",\"originalAdobeInstructionsExecuted\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
