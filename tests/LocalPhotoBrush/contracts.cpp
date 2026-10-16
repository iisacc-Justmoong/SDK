#include "PhotoBrush.h"

#include <iostream>
#include <limits>

int main() {
    try {
        using namespace photo_brush;
        namespace ga = iiGraphicAdjust;
        ga::Image source{64, 32, std::vector<ga::Rgba>(64 * 32)};
        std::vector<std::uint32_t> guidance(64 * 32);
        for (std::size_t y = 0; y < 32; ++y) for (std::size_t x = 0; x < 64; ++x) {
            const auto i = y * 64 + x;
            source.pixels[i] = x < 32 ? ga::Rgba{.6F, .2F, .1F, 1} : ga::Rgba{.1F, .2F, .7F, 1};
            guidance[i] = x < 32 ? 0xFF99331AU : 0xFF1A33B3U;
        }
        source.pixels[20 * 64 + 20] = {.9F, .1F, .6F, 0};
        guidance[20 * 64 + 20] = 0x00991A99U;
        const auto original = source.pixels;
        MaskPaintSession session;
        require(session.createMask(64, 32) && session.setColorGuidance(guidance), "session setup failed");
        LocalMaskBrushOptions brush;
        brush.sizePixels = 32; brush.feather = .5; brush.flow = .5;
        brush.density = .6; brush.strokeOpacity = .8;
        brush.autoMask = true; brush.autoMaskTolerance = .1;
        require(session.setBrushOptions(brush), "brush configuration failed");
        const auto blank = session.mask().coverage;
        require(session.processPointerEvent(pointer(PointerEventPhase::Press, 20, 16, 0)).accepted,
                "press failed");
        require(session.mask().coverage == blank && session.previewMask().coverage != blank,
                "preview must remain separate from committed mask");

        ga::CurvesOptions curve;
        curve.master.points = {{0, 0}, {.4, .6}, {1, 1}};
        const auto adjusted = ga::curves(source.view(), curve);
        require(bool(adjusted), "curve processing failed");
        const auto preview = ga::applyMaskedCorrection(source.view(), adjusted->view(),
                                                       maskView(session.previewMask()));
        require(bool(preview) && preview->pixels != source.pixels, "cross-module preview failed");
        session.cancelStroke();
        require(session.mask().coverage == blank && session.previewMask().coverage == blank,
                "cancel must restore exact committed coverage");
        require(!session.canUndo(), "cancel created history");

        require(session.processPointerEvent(pointer(PointerEventPhase::Press, 20, 16, 1)).accepted,
                "second press failed");
        require(session.processPointerEvent(pointer(PointerEventPhase::Move, 30, 16, 1.1)).accepted,
                "move failed");
        require(session.processPointerEvent(pointer(PointerEventPhase::Release, 40, 16, 1.2)).committed,
                "release commit failed");
        const auto committed = session.mask().coverage;
        require(committed != blank && committed == session.previewMask().coverage,
                "commit differs from current preview");
        for (std::size_t y = 0; y < 32; ++y) for (std::size_t x = 0; x < 64; ++x) {
            const auto coverage = committed[y * 64 + x];
            require(coverage >= 0 && coverage <= .6F, "local density ceiling violated");
            if (x >= 32) require(coverage == 0, "auto-mask leaked across strong source color boundary");
        }
        require(committed[20 * 64 + 20] == 0, "hidden RGB selected by auto-mask");
        ga::MaskedCorrectionOptions correctionOptions;
        correctionOptions.amount = 70;
        const auto result = ga::applyMaskedCorrection(source.view(), adjusted->view(),
                                                       maskView(session.mask()), correctionOptions);
        require(bool(result), "local composition failed");
        std::size_t changed = 0;
        for (std::size_t i = 0; i < committed.size(); ++i) {
            if (committed[i] == 0) require(result->pixels[i] == source.pixels[i], "zero mask pixel changed");
            if (result->pixels[i] != source.pixels[i]) ++changed;
            require(result->pixels[i].a == source.pixels[i].a, "source alpha changed");
        }
        require(changed > 0 && source.pixels == original, "input mutation or missing correction");
        require(session.undo() && session.mask().coverage == blank, "undo did not restore blank mask");
        require(session.redo() && session.mask().coverage == committed, "redo differs from committed mask");
        brush.mode = MaskBrushMode::Erase; brush.autoMask = false; brush.flow = 1; brush.strokeOpacity = 1;
        require(session.setBrushOptions(brush), "erase configuration failed");
        require(session.processPointerEvent(pointer(PointerEventPhase::Press, 20, 16, 2)).accepted
                && session.processPointerEvent(pointer(PointerEventPhase::Release, 20, 16, 2.1)).committed,
                "eraser stroke failed");
        require(session.mask().coverage[16 * 64 + 20] < committed[16 * 64 + 20], "eraser did not reduce coverage");
        require(session.undo() && session.mask().coverage == committed, "eraser undo failed");
        PixelMask invalid = session.mask();
        invalid.coverage.front() = std::numeric_limits<float>::quiet_NaN();
        require(!session.replaceMask(invalid) && session.mask().coverage == committed,
                "invalid replacement changed valid mask");
        const auto bad = ga::applyMaskedCorrection(source.view(), adjusted->view(), maskView(invalid));
        require(!bad && source.pixels == original, "invalid borrowed mask accepted or mutated source");
        std::cout << "Cross-module local correction contracts passed; changed pixels=" << changed << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
