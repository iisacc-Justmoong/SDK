#pragma once
#include "MolecularFormula.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ii::pbr {
struct Rgba8 { std::uint8_t r{},g{},b{},a{255}; };
class Texture {
public:
    [[nodiscard]] static Result<Texture> create(std::uint32_t width, std::uint32_t height,
                                                std::span<const Rgba8> pixels);
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::span<const Rgba8> pixels() const noexcept { return pixels_; }
private:
    std::uint32_t width_{},height_{};
    std::vector<Rgba8> pixels_;
};
class TextureDecoder {
public:
    virtual ~TextureDecoder()=default;
    // bytes are supplied by the application/file provider. Decode to RGBA8.
    virtual Result<Texture> decode(std::span<const std::byte> bytes,std::string_view contentType) const=0;
};
// Built-in binary PPM P6 decoder; PNG/JPEG and other formats can be supplied
// through TextureDecoder or through Texture::create() with decoded pixels.
class PpmDecoder final : public TextureDecoder {
public:
    [[nodiscard]] Result<Texture> decode(std::span<const std::byte>,std::string_view contentType) const override;
};
enum class AddressMode { clamp, repeat };
struct PaletteEntry { std::array<std::uint8_t,3> rgb{}; std::string materialId; };
class MaterialIdMap {
public:
    [[nodiscard]] static Result<MaterialIdMap> create(Texture image,std::vector<PaletteEntry> palette);
    // Nearest-neighbour categorical sampling; v=0 is the image top. No blending.
    // The returned view remains valid only while this unmodified map lives.
    [[nodiscard]] Result<std::string_view> sample(double u,double v,AddressMode mode=AddressMode::clamp) const;
    [[nodiscard]] const Texture& texture() const noexcept { return image_; }
private:
    Texture image_;
    std::map<std::array<std::uint8_t,3>,std::string> palette_;
};
struct Color4 { double r{1},g{1},b{1},a{1}; };
struct SurfaceAppearance {
    Color4 baseColorFactor{}; // linear
    double metallicFactor{1},roughnessFactor{1};
    std::shared_ptr<const Texture> baseColorTexture; // RGB sRGB, alpha linear
    std::shared_ptr<const Texture> metallicRoughnessTexture; // G roughness, B metalness, linear
};
struct SurfaceSample { Color4 baseColor; double metallic{},roughness{}; };
[[nodiscard]] Result<SurfaceSample> sampleSurface(const SurfaceAppearance&,double u,double v,
                                                  AddressMode mode=AddressMode::clamp);
}
