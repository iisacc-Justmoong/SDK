#include "BoxCad.h"
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace ii::volumetric {
namespace {
template<class T> Result<T> fail(ErrorCode code, const char* message) {
    return std::unexpected(Error{code, message});
}
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool product(std::size_t a, std::size_t b, std::size_t& out) {
    if (b && a > std::numeric_limits<std::size_t>::max()/b) return false;
    out=a*b; return true;
}
}
Result<std::shared_ptr<const BoxCad>> BoxCad::create(
    std::string id, std::uint64_t revision, Vec3 origin, Vec3 dimensions, RegionId region) {
    if (id.empty() || !finite(origin) || !finite(dimensions) ||
        dimensions.x<=0 || dimensions.y<=0 || dimensions.z<=0 ||
        !finite({origin.x+dimensions.x,origin.y+dimensions.y,origin.z+dimensions.z}))
        return fail<std::shared_ptr<const BoxCad>>(ErrorCode::invalid_argument, "box requires an identity and finite positive dimensions");
    try { return std::shared_ptr<const BoxCad>(new BoxCad(std::move(id),revision,origin,dimensions,region)); }
    catch (const std::bad_alloc&) { return fail<std::shared_ptr<const BoxCad>>(ErrorCode::limit_exceeded,"box allocation failed"); }
}
Result<TetrahedralMesh> BoxMesher::mesh(const CadDesign& design, const MeshingOptions& options) const {
    const auto* box=dynamic_cast<const BoxCad*>(&design);
    if (!box) return fail<TetrahedralMesh>(ErrorCode::invalid_argument,"box mesher requires a BoxCad design");
    const std::size_t nx=options.subdivisions[0], ny=options.subdivisions[1], nz=options.subdivisions[2];
    if (!nx || !ny || !nz) return fail<TetrahedralMesh>(ErrorCode::invalid_argument,"box subdivisions must be positive");
    if (nx==std::numeric_limits<std::size_t>::max() || ny==std::numeric_limits<std::size_t>::max() || nz==std::numeric_limits<std::size_t>::max())
        return fail<TetrahedralMesh>(ErrorCode::limit_exceeded,"box subdivision is too large");
    std::size_t xy=0, count=0, cells=0, tets=0;
    if (!product(nx+1,ny+1,xy) || !product(xy,nz+1,count) ||
        !product(nx,ny,xy) || !product(xy,nz,cells) || !product(cells,6,tets) ||
        count>options.maxVertices || tets>options.maxTetrahedra ||
        count>std::numeric_limits<std::uint32_t>::max())
        return fail<TetrahedralMesh>(ErrorCode::limit_exceeded,"box subdivision exceeds mesh limits");
    try {
        TetrahedralMesh result; result.vertices.reserve(count); result.tetrahedra.reserve(tets);
        const auto o=box->origin(), d=box->dimensions();
        for (std::size_t z=0;z<=nz;++z) for (std::size_t y=0;y<=ny;++y) for (std::size_t x=0;x<=nx;++x)
            result.vertices.push_back({o.x+d.x*(double(x)/double(nx)),
                                       o.y+d.y*(double(y)/double(ny)),
                                       o.z+d.z*(double(z)/double(nz))});
        auto index=[&](std::size_t x,std::size_t y,std::size_t z) {
            return static_cast<std::uint32_t>((z*(ny+1)+y)*(nx+1)+x);
        };
        constexpr std::array<std::array<unsigned,4>,6> tetrahedra{{
            {0,1,3,7}, {0,1,5,7}, {0,2,3,7}, {0,2,6,7}, {0,4,5,7}, {0,4,6,7}
        }};
        for (std::size_t z=0;z<nz;++z) for (std::size_t y=0;y<ny;++y) for (std::size_t x=0;x<nx;++x) {
            const std::array<std::uint32_t,8> v{
                index(x,y,z),index(x+1,y,z),index(x,y+1,z),index(x+1,y+1,z),
                index(x,y,z+1),index(x+1,y,z+1),index(x,y+1,z+1),index(x+1,y+1,z+1)};
            for (const auto& t:tetrahedra)
                result.tetrahedra.push_back({{v[t[0]],v[t[1]],v[t[2]],v[t[3]]},box->region()});
        }
        return result;
    } catch (const std::bad_alloc&) { return fail<TetrahedralMesh>(ErrorCode::limit_exceeded,"box mesh allocation failed"); }
}
}
