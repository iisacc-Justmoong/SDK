#pragma once
#include <Texture.h>
namespace ii::material_app {
// Application-owned bridge to iiVisualCodec's memory-only image decoder.
class VisualTextureDecoder final : public pbr::TextureDecoder {
public:
    [[nodiscard]] pbr::Result<pbr::Texture> decode(std::span<const std::byte>,std::string_view contentType) const override;
};
}
