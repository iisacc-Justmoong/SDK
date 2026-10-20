#include <Filter/FilterPipeline.h>
#include <iiGraphicAdjust/BlurFilters.h>
#include <iiGraphicAdjust/PixelFilters.h>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
template<class F> void measure(const char* name, F f) {
    const auto start = std::chrono::steady_clock::now();
    const auto result = f();
    std::cout << name << ' ' << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count() << ' ' << result << '\n';
}
int main() {
    auto surface = makeDrawingSurface(384, 384);
    std::uint32_t seed=11;
    for(auto& p: surface.pixels) {seed=seed*1664525U+1013904223U;p=seed;}
    for(auto radius: {1., 12.}) {
        const auto name="paint_blur_"+std::to_string(int(radius));
        measure(name.c_str(), [&]{auto copy=surface;applyFilterPipeline(copy, {}, {{{true, FilterKind::Blur, radius, 1.}}});std::uint64_t sum=0;for(auto p:copy.pixels)sum+=p;return sum;});
    }
    iiGraphicAdjust::Image image{768, 768, {}};
    for (std::size_t i=0;i<image.width*image.height;++i) {seed=seed*1664525U+1013904223U;image.pixels.push_back({float(seed&255)/255,float((seed>>8)&255)/255,float((seed>>16)&255)/255,float((seed>>24)|1)/255});}
    measure("graphic_gaussian_12", [&]{auto result=iiGraphicAdjust::filterGaussianBlur(image.view(),12);if(!result)throw std::runtime_error("filter");std::uint64_t sum=0;for(auto p:result->pixels)for(auto c:{p.r,p.g,p.b,p.a})sum+=std::bit_cast<std::uint32_t>(c);return sum;});
    image.width=128;image.height=128;image.pixels.resize(128*128);
    measure("graphic_halftone", [&]{auto result=iiGraphicAdjust::colorHalftone(image.view());if(!result)throw std::runtime_error("filter");std::uint64_t sum=0;for(auto p:result->pixels)for(auto c:{p.r,p.g,p.b,p.a})sum+=std::bit_cast<std::uint32_t>(c);return sum;});
}
