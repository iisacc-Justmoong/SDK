#pragma once

#include "MaterialExchange.h"

namespace ii::pbr::exchange {
// Format-neutral input owned by the caller; its metadata remains available to
// a replacement converter even if that implementation rejects the texture.
struct TextureConversionInput {
    std::string original;
    TextureMetadata metadata;
};

// Host-owned converter interface. The host selects and executes one converter;
// no converter owns or invokes another execution object. Implementations must
// preserve original bytes and metadata in both successful and failed results.
class TextureConverter {
public:
    virtual ~TextureConverter() = default;
    [[nodiscard]] virtual MaterialExchangeResult execute(const TextureConversionInput& input) const = 0;
};

// A binding operation receives original bytes, their interpretation, and exact
// node libraries. The caller owns all source resources and supplies their data.
struct TextureBindingRequest {
    std::string original;
    TextureMetadata metadata;
    std::vector<MaterialLibrary> libraries;
};

// Produce a MaterialX color3 or color4 image-node document for explicitly
// described opaque RGB or straight RGBA8 PNG base-color textures. The latter
// requires its alpha association metadata to be retained by the host. PNG IDAT
// and checksums are not decoded or checked. Texture bytes and metadata survive
// ordinary failures; unsupported layouts or digests return diagnostics. No I/O.
[[nodiscard]] MaterialExchangeResult convertTextureToMaterialX(const TextureBindingRequest& request);

// MaterialX adapter: the constructor owns only the explicitly supplied library
// bytes and metadata. The host controls object lifetime and call serialization.
class MaterialXTextureConverter final : public TextureConverter {
public:
    explicit MaterialXTextureConverter(std::vector<MaterialLibrary> libraries);
    [[nodiscard]] MaterialExchangeResult execute(const TextureConversionInput& input) const override;
private:
    std::vector<MaterialLibrary> libraries_;
};
} // namespace ii::pbr::exchange
