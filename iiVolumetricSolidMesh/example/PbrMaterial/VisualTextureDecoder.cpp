#include "VisualTextureDecoder.h"
#include <iiVisualCodec/ImageCodec.h>
#include <limits>
#include <new>

namespace ii::material_app {
pbr::Result<pbr::Texture> VisualTextureDecoder::decode(std::span<const std::byte> input,std::string_view contentType) const {
    if(contentType!="image/png"&&contentType!="image/jpeg"&&contentType!="image/webp"&&contentType!="image/tiff"&&contentType!="image/bmp"&&contentType!="image/tga")
        return std::unexpected(pbr::Error{pbr::ErrorCode::unsupported,"unsupported image content type"});
    auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(input.data()),input.size());
    ii::visual::image::Limits limits;
    limits.pixel_bytes=16777216u*4;
    auto info=ii::visual::image::inspect(bytes,limits);
    if(!info)return std::unexpected(pbr::Error{pbr::ErrorCode::invalid_input,info.error().message});
    const auto format=info->format;
    if((contentType=="image/png"&&format!=ii::visual::image::Format::png)||
       (contentType=="image/jpeg"&&format!=ii::visual::image::Format::jpeg)||
       (contentType=="image/webp"&&format!=ii::visual::image::Format::webp)||
       (contentType=="image/tiff"&&format!=ii::visual::image::Format::tiff)||
       (contentType=="image/bmp"&&format!=ii::visual::image::Format::bmp)||
       (contentType=="image/tga"&&format!=ii::visual::image::Format::tga))
        return std::unexpected(pbr::Error{pbr::ErrorCode::invalid_input,"image content type does not match encoded bytes"});
    if(!info->width||!info->height||std::size_t(info->width)>16777216u/std::size_t(info->height))
        return std::unexpected(pbr::Error{pbr::ErrorCode::limit_exceeded,"texture dimensions exceed RGBA limits"});
    auto image=ii::visual::image::decode(bytes,limits);
    if(!image)return std::unexpected(pbr::Error{pbr::ErrorCode::backend_failure,image.error().message});
    if(!image->width||!image->height||std::size_t(image->width)>16777216u/std::size_t(image->height))
        return std::unexpected(pbr::Error{pbr::ErrorCode::limit_exceeded,"decoded texture exceeds RGBA limits"});
    const auto count=std::size_t(image->width)*image->height;
    if(image->rgba.size()!=count*4)
        return std::unexpected(pbr::Error{pbr::ErrorCode::invalid_input,"decoded RGBA size does not match dimensions"});
    try {
        std::vector<pbr::Rgba8> pixels;pixels.reserve(count);
        for(std::size_t i=0;i<count;++i) pixels.push_back({image->rgba[i*4],image->rgba[i*4+1],image->rgba[i*4+2],image->rgba[i*4+3]});
        return pbr::Texture::create(image->width,image->height,pixels);
    } catch(const std::bad_alloc&) {return std::unexpected(pbr::Error{pbr::ErrorCode::limit_exceeded,"decoded texture allocation failed"});}
}
}
