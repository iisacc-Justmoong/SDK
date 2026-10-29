#include "Texture.h"
#include <algorithm>
#include <bit>
#include <limits>

namespace ii::webgpu {
namespace {
struct Block final { std::uint32_t width=1, height=1, bytes=0; };
Block block(WGPUTextureFormat format) {
    switch (format) {
    case WGPUTextureFormat_R8Unorm: case WGPUTextureFormat_R8Snorm:
    case WGPUTextureFormat_R8Uint: case WGPUTextureFormat_R8Sint: case WGPUTextureFormat_Stencil8: return {1,1,1};
    case WGPUTextureFormat_R16Uint: case WGPUTextureFormat_R16Sint: case WGPUTextureFormat_R16Float:
    case WGPUTextureFormat_RG8Unorm: case WGPUTextureFormat_RG8Snorm: case WGPUTextureFormat_RG8Uint:
    case WGPUTextureFormat_RG8Sint: case WGPUTextureFormat_Depth16Unorm: return {1,1,2};
    case WGPUTextureFormat_R32Float: case WGPUTextureFormat_R32Uint: case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_RG16Uint: case WGPUTextureFormat_RG16Sint: case WGPUTextureFormat_RG16Float:
    case WGPUTextureFormat_RGBA8Unorm: case WGPUTextureFormat_RGBA8UnormSrgb: case WGPUTextureFormat_RGBA8Snorm:
    case WGPUTextureFormat_RGBA8Uint: case WGPUTextureFormat_RGBA8Sint: case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb: case WGPUTextureFormat_RGB10A2Uint: case WGPUTextureFormat_RGB10A2Unorm:
    case WGPUTextureFormat_RG11B10Ufloat: case WGPUTextureFormat_RGB9E5Ufloat:
    case WGPUTextureFormat_Depth24Plus: case WGPUTextureFormat_Depth24PlusStencil8: case WGPUTextureFormat_Depth32Float: return {1,1,4};
    case WGPUTextureFormat_RG32Float: case WGPUTextureFormat_RG32Uint: case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RGBA16Uint: case WGPUTextureFormat_RGBA16Sint: case WGPUTextureFormat_RGBA16Float:
    case WGPUTextureFormat_Depth32FloatStencil8: return {1,1,8};
    case WGPUTextureFormat_RGBA32Float: case WGPUTextureFormat_RGBA32Uint: case WGPUTextureFormat_RGBA32Sint: return {1,1,16};
    case WGPUTextureFormat_BC1RGBAUnorm: case WGPUTextureFormat_BC1RGBAUnormSrgb:
    case WGPUTextureFormat_BC4RUnorm: case WGPUTextureFormat_BC4RSnorm:
    case WGPUTextureFormat_ETC2RGB8Unorm: case WGPUTextureFormat_ETC2RGB8UnormSrgb:
    case WGPUTextureFormat_ETC2RGB8A1Unorm: case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
    case WGPUTextureFormat_EACR11Unorm: case WGPUTextureFormat_EACR11Snorm: return {4,4,8};
    case WGPUTextureFormat_BC2RGBAUnorm: case WGPUTextureFormat_BC2RGBAUnormSrgb:
    case WGPUTextureFormat_BC3RGBAUnorm: case WGPUTextureFormat_BC3RGBAUnormSrgb:
    case WGPUTextureFormat_BC5RGUnorm: case WGPUTextureFormat_BC5RGSnorm:
    case WGPUTextureFormat_BC6HRGBUfloat: case WGPUTextureFormat_BC6HRGBFloat:
    case WGPUTextureFormat_BC7RGBAUnorm: case WGPUTextureFormat_BC7RGBAUnormSrgb:
    case WGPUTextureFormat_ETC2RGBA8Unorm: case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
    case WGPUTextureFormat_EACRG11Unorm: case WGPUTextureFormat_EACRG11Snorm: return {4,4,16};
#define IIWEBGPU_ASTC(W,H) case WGPUTextureFormat_ASTC##W##x##H##Unorm: case WGPUTextureFormat_ASTC##W##x##H##UnormSrgb: return {W,H,16};
    IIWEBGPU_ASTC(4,4) IIWEBGPU_ASTC(5,4) IIWEBGPU_ASTC(5,5) IIWEBGPU_ASTC(6,5)
    IIWEBGPU_ASTC(6,6) IIWEBGPU_ASTC(8,5) IIWEBGPU_ASTC(8,6) IIWEBGPU_ASTC(8,8)
    IIWEBGPU_ASTC(10,5) IIWEBGPU_ASTC(10,6) IIWEBGPU_ASTC(10,8) IIWEBGPU_ASTC(10,10)
    IIWEBGPU_ASTC(12,10) IIWEBGPU_ASTC(12,12)
#undef IIWEBGPU_ASTC
    default: return {};
    }
}
bool multiply(std::uint64_t& value, std::uint64_t factor) {
    if (factor && value > std::numeric_limits<std::uint64_t>::max()/factor) return false;
    value *= factor; return true;
}
}
Result<std::uint64_t> texture_bytes(const WGPUTextureDescriptor& descriptor) {
    auto width = std::uint64_t(descriptor.size.width), height = std::uint64_t(descriptor.size.height);
    auto depth = std::uint64_t(descriptor.size.depthOrArrayLayers);
    const auto format = block(descriptor.format);
    if (!format.bytes) return std::unexpected(Error{ErrorCode::Unsupported, "texture format has no managed footprint; use the native API for implementation extensions"});
    if (!width || !height || !depth || !descriptor.mipLevelCount || (descriptor.sampleCount != 1 && descriptor.sampleCount != 4) ||
        (descriptor.dimension != WGPUTextureDimension_1D && descriptor.dimension != WGPUTextureDimension_2D && descriptor.dimension != WGPUTextureDimension_3D) ||
        (descriptor.dimension == WGPUTextureDimension_1D && (height != 1 || depth != 1)))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "invalid texture extent, dimension, samples or mip count"});
    const auto maximum = descriptor.dimension == WGPUTextureDimension_3D ? std::max({width,height,depth}) : std::max(width,height);
    if (descriptor.mipLevelCount > static_cast<std::uint32_t>(std::bit_width(maximum)))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "mip count exceeds the extent"});
    std::uint64_t total = 0;
    for (std::uint32_t mip=0; mip<descriptor.mipLevelCount; ++mip) {
        std::uint64_t bytes = (width + format.width - 1)/format.width;
        if (!multiply(bytes, (height + format.height - 1)/format.height) || !multiply(bytes, depth) ||
            !multiply(bytes, format.bytes) || !multiply(bytes, descriptor.sampleCount) || bytes > std::numeric_limits<std::uint64_t>::max()-total)
            return std::unexpected(Error{ErrorCode::InvalidArgument, "texture footprint overflow"});
        total += bytes;
        width = std::max<std::uint64_t>(1,width/2); height = std::max<std::uint64_t>(1,height/2);
        if (descriptor.dimension == WGPUTextureDimension_3D) depth = std::max<std::uint64_t>(1,depth/2);
    }
    return total;
}
}
