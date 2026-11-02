#include "Texture.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace ii::pbr {
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string text) { return std::unexpected(Error{code,std::move(text)}); }
constexpr std::size_t maxPixels=16777216;
Result<std::size_t> pixelCount(std::uint32_t w,std::uint32_t h) {
    if (!w||!h) return fail<std::size_t>(ErrorCode::invalid_input,"texture dimensions must be positive");
    if (std::size_t(w)>maxPixels/std::size_t(h)) return fail<std::size_t>(ErrorCode::limit_exceeded,"texture is too large");
    return std::size_t(w)*h;
}
Result<std::size_t> at(const Texture& image,double u,double v,AddressMode mode) {
    auto count=pixelCount(image.width(),image.height());
    if (!count || image.pixels().size()!=*count) return fail<std::size_t>(ErrorCode::invalid_input,"invalid texture");
    if (!std::isfinite(u)||!std::isfinite(v)) return fail<std::size_t>(ErrorCode::invalid_input,"non-finite texture coordinate");
    if (mode==AddressMode::clamp) { u=std::clamp(u,0.0,1.0); v=std::clamp(v,0.0,1.0); }
    else if (mode==AddressMode::repeat) { u-=std::floor(u); v-=std::floor(v); }
    else return fail<std::size_t>(ErrorCode::invalid_input,"unknown texture address mode");
    const auto x=std::min<std::size_t>(static_cast<std::size_t>(u*image.width()),image.width()-1);
    const auto y=std::min<std::size_t>(static_cast<std::size_t>(v*image.height()),image.height()-1);
    return y*image.width()+x;
}
bool inUnit(double x) { return std::isfinite(x)&&x>=0&&x<=1; }
double linear(double s) { return s<=0.04045?s/12.92:std::pow((s+0.055)/1.055,2.4); }
}
Result<Texture> Texture::create(std::uint32_t w,std::uint32_t h,std::span<const Rgba8> values) {
    auto n=pixelCount(w,h); if(!n)return std::unexpected(n.error());
    if(values.size()!=*n) return fail<Texture>(ErrorCode::invalid_input,"RGBA pixel count does not match dimensions");
    try { Texture result; result.width_=w;result.height_=h;result.pixels_.assign(values.begin(),values.end());return result; }
    catch(const std::bad_alloc&) { return fail<Texture>(ErrorCode::limit_exceeded,"texture allocation failed"); }
}
Result<Texture> PpmDecoder::decode(std::span<const std::byte> bytes,std::string_view contentType) const {
    if(bytes.empty()) return fail<Texture>(ErrorCode::invalid_input,"empty PPM texture");
    if(bytes.size()>64u*1024u*1024u) return fail<Texture>(ErrorCode::limit_exceeded,"PPM input exceeds the byte limit");
    if (!contentType.empty()&&contentType!="image/x-portable-pixmap"&&contentType!="image/ppm")
        return fail<Texture>(ErrorCode::unsupported,"PPM decoder supports image/x-portable-pixmap only");
    auto ch=[&](std::size_t i){return static_cast<unsigned char>(bytes[i]);};
    auto ws=[](unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\v';};
    std::size_t pos=0;
    auto token=[&]() -> std::string_view {
        while(pos<bytes.size()) {
            if(ws(ch(pos))) { ++pos; continue; }
            if(ch(pos)=='#') { while(pos<bytes.size()&&ch(pos)!='\n'&&ch(pos)!='\r')++pos; continue; }
            break;
        }
        const auto start=pos;
        while(pos<bytes.size()&&!ws(ch(pos))&&ch(pos)!='#')++pos;
        return {reinterpret_cast<const char*>(bytes.data()+start),pos-start};
    };
    auto number=[](std::string_view s) -> std::optional<std::uint32_t> {
        if(s.empty())return std::nullopt; std::uint32_t n=0;
        for(char c:s) { if(c<'0'||c>'9'||n>(std::numeric_limits<std::uint32_t>::max()-unsigned(c-'0'))/10)return std::nullopt; n=n*10+unsigned(c-'0'); }
        return n;
    };
    const auto magic=token(); if(magic!="P6")return fail<Texture>(ErrorCode::unsupported,"expected binary PPM P6");
    const auto width=number(token()),height=number(token()),maximum=number(token());
    if(!width||!height||!maximum||*maximum!=255||pos>=bytes.size()||!ws(ch(pos)))
        return fail<Texture>(ErrorCode::invalid_input,"invalid PPM header or unsupported max value");
    auto n=pixelCount(*width,*height); if(!n)return std::unexpected(n.error());
    // Consume only the required separator, not arbitrary whitespace that may be pixel data.
    const auto sep=ch(pos++); if(sep=='\r'&&pos<bytes.size()&&ch(pos)=='\n')++pos;
    if(bytes.size()-pos!=*n*3) return fail<Texture>(ErrorCode::invalid_input,"PPM payload length does not match dimensions");
    try {
        std::vector<Rgba8> rgba; rgba.reserve(*n);
        for(std::size_t i=0;i<*n;++i) rgba.push_back({ch(pos+3*i),ch(pos+3*i+1),ch(pos+3*i+2),255});
        return Texture::create(*width,*height,rgba);
    } catch(const std::bad_alloc&) { return fail<Texture>(ErrorCode::limit_exceeded,"PPM allocation failed"); }
}
Result<MaterialIdMap> MaterialIdMap::create(Texture image,std::vector<PaletteEntry> palette) {
    auto n=pixelCount(image.width(),image.height());
    if(!n||image.pixels().size()!=*n) return fail<MaterialIdMap>(ErrorCode::invalid_input,"invalid material ID texture");
    if(palette.empty())return fail<MaterialIdMap>(ErrorCode::invalid_input,"material ID palette is empty");
    try {
        MaterialIdMap result; result.image_=std::move(image);
        for(auto& entry:palette) {
            if(entry.materialId.empty()||!result.palette_.emplace(entry.rgb,std::move(entry.materialId)).second)
                return fail<MaterialIdMap>(ErrorCode::invalid_input,"duplicate color or empty material ID in palette");
        }
        for(auto p:result.image_.pixels())
            if(p.a!=255||!result.palette_.contains({p.r,p.g,p.b}))
                return fail<MaterialIdMap>(ErrorCode::invalid_input,"each opaque material ID pixel must have a palette entry");
        return result;
    } catch(const std::bad_alloc&) { return fail<MaterialIdMap>(ErrorCode::limit_exceeded,"material palette allocation failed"); }
}
Result<std::string_view> MaterialIdMap::sample(double u,double v,AddressMode mode) const {
    auto i=at(image_,u,v,mode);if(!i)return std::unexpected(i.error());
    const auto p=image_.pixels()[*i]; const auto e=palette_.find({p.r,p.g,p.b});
    if(e==palette_.end())return fail<std::string_view>(ErrorCode::not_found,"material ID color is not mapped");
    return std::string_view(e->second);
}
Result<SurfaceSample> sampleSurface(const SurfaceAppearance& a,double u,double v,AddressMode mode) {
    if (!inUnit(a.baseColorFactor.r)||!inUnit(a.baseColorFactor.g)||!inUnit(a.baseColorFactor.b)||!inUnit(a.baseColorFactor.a)||
        !inUnit(a.metallicFactor)||!inUnit(a.roughnessFactor)||!std::isfinite(u)||!std::isfinite(v)||
        (mode!=AddressMode::clamp&&mode!=AddressMode::repeat))
        return fail<SurfaceSample>(ErrorCode::invalid_input,"invalid PBR factor or texture coordinate");
    SurfaceSample out{a.baseColorFactor,a.metallicFactor,a.roughnessFactor};
    if(a.baseColorTexture) {
        auto i=at(*a.baseColorTexture,u,v,mode);if(!i)return std::unexpected(i.error()); const auto p=a.baseColorTexture->pixels()[*i];
        out.baseColor.r*=linear(p.r/255.0);out.baseColor.g*=linear(p.g/255.0);out.baseColor.b*=linear(p.b/255.0);out.baseColor.a*=p.a/255.0;
    }
    if(a.metallicRoughnessTexture) {
        auto i=at(*a.metallicRoughnessTexture,u,v,mode);if(!i)return std::unexpected(i.error()); const auto p=a.metallicRoughnessTexture->pixels()[*i];
        out.roughness*=p.g/255.0;out.metallic*=p.b/255.0;
    }
    return out;
}
}
