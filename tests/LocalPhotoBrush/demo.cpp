#include "PhotoBrush.h"

#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    try {
        using namespace photo_brush;
        require(argc == 2, "usage: LocalPhotoBrush OUTPUT_DIRECTORY");
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        const auto source = syntheticImage(256, 144);
        const auto before = source.pixels;
        const auto words = iiGraphicAdjust::toArgb32(source.view());
        require(bool(words), "guidance conversion failed");
        MaskPaintSession brush;
        require(brush.createMask(256, 144) && brush.setColorGuidance(*words), "mask creation failed");
        LocalMaskBrushOptions options;
        options.sizePixels = 80; options.feather = .8; options.flow = .4;
        options.density = .75; options.strokeOpacity = .85;
        options.autoMask = true; options.autoMaskTolerance = .35;
        require(brush.setBrushOptions(options), "brush settings failed");
        require(brush.processPointerEvent(pointer(PointerEventPhase::Press, 50, 55, 0)).accepted,
                "brush press failed");
        require(brush.processPointerEvent(pointer(PointerEventPhase::Move, 100, 75, .1)).accepted,
                "brush move failed");
        require(brush.processPointerEvent(pointer(PointerEventPhase::Release, 160, 75, .2)).committed,
                "brush release failed");

        const auto corrected = correction(source);
        iiGraphicAdjust::MaskedCorrectionOptions apply;
        apply.amount = 80;
        const auto local = iiGraphicAdjust::applyMaskedCorrection(source.view(), corrected.view(),
                                                                 maskView(brush.mask()), apply);
        require(bool(local), "masked correction failed");
        std::size_t selected = 0, changed = 0, untouched = 0;
        iiGraphicAdjust::Image mask{source.width, source.height, {}};
        mask.pixels.reserve(source.pixels.size());
        for (std::size_t i = 0; i < source.pixels.size(); ++i) {
            const float coverage = brush.mask().coverage[i];
            mask.pixels.push_back({coverage, coverage, coverage, 1});
            if (coverage == 0) {
                require(local->pixels[i] == source.pixels[i], "unselected pixel changed");
                ++untouched;
            } else ++selected;
            if (local->pixels[i] != source.pixels[i]) ++changed;
            require(local->pixels[i].a == source.pixels[i].a, "alpha changed");
        }
        require(selected > 0 && changed > 0 && untouched > 0 && source.pixels == before,
                "example did not produce a detached local correction");
        saveImage(output / "source.png", source.view());
        saveImage(output / "mask.png", mask.view());
        saveImage(output / "corrected-entire.png", corrected.view());
        saveImage(output / "corrected-local.png", local->view());
        iiGraphicAdjust::Image comparison{source.width * 3, source.height, {}};
        comparison.pixels.reserve(source.pixels.size() * 3);
        const std::array<const iiGraphicAdjust::Image*, 3> panels{&source, &mask, &*local};
        for (std::size_t y = 0; y < source.height; ++y) {
            const auto start = y * source.width;
            for (const auto* panel : panels)
                comparison.pixels.insert(comparison.pixels.end(), panel->pixels.begin() + start,
                                         panel->pixels.begin() + start + source.width);
        }
        saveImage(output / "comparison.png", comparison.view());
        std::ofstream report(output / "result.json");
        report << "{\n  \"syntheticInput\": true,\n  \"pixels\": " << source.pixels.size()
               << ",\n  \"selectedPixels\": " << selected << ",\n  \"changedPixels\": " << changed
               << ",\n  \"exactlyUnchangedOutsideMask\": " << untouched
               << ",\n  \"sourceUnmodified\": true,\n  \"alphaPreserved\": true,"
                  "\n  \"adobePixelParity\": false\n}\n";
        require(bool(report), "result report write failed");
        std::cout << "Local correction: " << changed << " pixels changed, " << untouched
                  << " unselected pixels exactly preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
