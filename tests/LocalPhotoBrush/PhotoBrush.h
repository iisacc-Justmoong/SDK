#pragma once

#include <iiPaintEngine>
#include <iiGraphicAdjust/iiGraphicAdjust.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace photo_brush {
inline void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

inline PointerEvent pointer(PointerEventPhase phase, double x, double y, double time) {
    PointerEvent event;
    event.device = PointerDeviceKind::Tablet;
    event.tool = PointerToolKind::Pen;
    event.phase = phase;
    event.button = phase == PointerEventPhase::Press ? PointerButton::Primary : PointerButton::None;
    event.primaryButtonDown = phase != PointerEventPhase::Release && phase != PointerEventPhase::Cancel;
    event.documentPosition = {x, y};
    event.pressure = .75;
    event.time = time;
    return event;
}

inline iiGraphicAdjust::MaskView maskView(const PixelMask& mask) {
    return {std::size_t(mask.width), std::size_t(mask.height), mask.coverage};
}

inline iiGraphicAdjust::Image syntheticImage(std::size_t width, std::size_t height) {
    iiGraphicAdjust::Image image{width, height, {}};
    image.pixels.reserve(width * height);
    for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) {
        const float horizontal = float(x) / float(width), vertical = float(y) / float(height);
        const float fine = float((x + 3 * y) % 11) / 220.F;
        const bool warm = x < width / 2;
        image.pixels.push_back({.18F + horizontal * .33F + (warm ? .12F : 0.F) + fine,
                                .17F + vertical * .3F + fine,
                                .12F + horizontal * .18F + (warm ? 0.F : .3F) + fine, 1.F});
    }
    return image;
}

inline iiGraphicAdjust::Image correction(const iiGraphicAdjust::Image& image) {
    namespace ga = iiGraphicAdjust;
    ga::WhiteBalanceOptions balance;
    balance.temperature = 7200; balance.tint = 8;
    auto stage = ga::whiteBalance(image.view(), balance);
    require(bool(stage), "white balance failed");
    ga::DevelopLightOptions light;
    light.exposure = .7; light.highlights = -20; light.shadows = 20;
    stage = ga::developLight(stage->view(), light);
    require(bool(stage), "light correction failed");
    ga::DevelopCurveOptions curve;
    curve.master.points = {{0, 0}, {.3, .37}, {.7, .8}, {1, 1}};
    curve.red.points = {{0, 0}, {.5, .53}, {1, 1}};
    stage = ga::developCurve(stage->view(), curve);
    require(bool(stage), "point curve failed");
    ga::DevelopColorOptions color;
    color.saturation = 18; color.vibrance = 22;
    color.mixer[std::size_t(ga::MixerBand::Orange)].luminance = 7;
    stage = ga::developColor(stage->view(), color);
    require(bool(stage), "color correction failed");
    ga::DevelopDetailOptions detail;
    detail.sharpenAmount = 35; detail.luminanceNoise = 5;
    stage = ga::developDetail(stage->view(), detail);
    require(bool(stage), "detail correction failed");
    ga::FilmGrainOptions grain;
    grain.amount = 12; grain.size = 25; grain.roughness = 40; grain.seed = 20261014;
    stage = ga::filmGrain(stage->view(), grain);
    require(bool(stage), "grain correction failed");
    return std::move(*stage);
}

inline void saveImage(const std::filesystem::path& path, iiGraphicAdjust::ImageView image) {
    auto words = iiGraphicAdjust::toArgb32(image);
    require(bool(words), "image conversion failed");
    BitmapFile bitmap;
    require(bitmap.create(path, Types::Pixel(image.width), Types::Pixel(image.height), "png"),
            "output bitmap creation failed");
    RasterLayer layer{Types::Pixel(image.width), Types::Pixel(image.height), std::move(*words)};
    require(bitmap.replacePixels(layer) && bitmap.save(), "output PNG save failed");
}
} // namespace photo_brush
