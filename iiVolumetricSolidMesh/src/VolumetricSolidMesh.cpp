#include "VolumetricSolidMesh.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <type_traits>
#include <utility>

namespace ii::volumetric {
namespace {
// Transactional commit writes into the existing buffers so client spans stay
// valid. A future throwing state assignment would break that guarantee.
static_assert(std::is_nothrow_copy_assignable_v<ParticleDynamics>);
static_assert(std::is_nothrow_copy_assignable_v<NodeDynamics>);
template<class T> Result<T> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code,std::move(message)});
}
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(const std::array<double,9>& values) {
    return std::all_of(values.begin(),values.end(),[](double v){return std::isfinite(v);});
}
struct LVec { long double x{},y{},z{}; };
LVec v(Vec3 p) { return {p.x,p.y,p.z}; }
LVec add(LVec a,LVec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
LVec sub(LVec a,LVec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
LVec mul(LVec a,long double s) { return {a.x*s,a.y*s,a.z*s}; }
long double dot(LVec a,LVec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
LVec cross(LVec a,LVec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
long double length(LVec a) { return std::hypot(a.x,a.y,a.z); }
bool representable(long double x) {
    return std::isfinite(x) && std::abs(x)<=std::numeric_limits<double>::max();
}
Result<long double> signedSixVolume(Vec3 a,Vec3 b,Vec3 c,Vec3 d) {
    const auto u=sub(v(b),v(a)), w=sub(v(c),v(a)), x=sub(v(d),v(a));
    const auto lu=length(u), lw=length(w), lx=length(x);
    if (!(lu>0 && lw>0 && lx>0) || !std::isfinite(lu) || !std::isfinite(lw) || !std::isfinite(lx))
        return fail<long double>(ErrorCode::invalid_geometry,"tetrahedron has a collapsed edge");
    const long double normalized=dot(mul(u,1/lu),cross(mul(w,1/lw),mul(x,1/lx)));
    if (!std::isfinite(normalized) || std::abs(normalized)<=64*std::numeric_limits<double>::epsilon())
        return fail<long double>(ErrorCode::invalid_geometry,"tetrahedron is degenerate or numerically unstable");
    const auto six=normalized*lu*lw*lx;
    if (!representable(six) || six==0)
        return fail<long double>(ErrorCode::invalid_geometry,"tetrahedron volume cannot be represented");
    return six;
}
LVec centroid(const TetrahedralMesh& mesh,const Tetrahedron& tet) {
    const auto a=v(mesh.vertices[tet.vertices[0]]);
    return add(a,mul(add(add(sub(v(mesh.vertices[tet.vertices[1]]),a),
                             sub(v(mesh.vertices[tet.vertices[2]]),a)),
                         sub(v(mesh.vertices[tet.vertices[3]]),a)),0.25L));
}
struct Face { std::array<std::uint32_t,3> oriented; RegionId region; unsigned count=1; };
bool sameOrientation(const std::array<std::uint32_t,3>& a,const std::array<std::uint32_t,3>& b) {
    return (a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]) ||
           (a[0]==b[1]&&a[1]==b[2]&&a[2]==b[0]) ||
           (a[0]==b[2]&&a[1]==b[0]&&a[2]==b[1]);
}
struct Element { long double volume, mass; MaterialId material; };

Result<void> validateProperties(const std::vector<Material>& materials,const std::vector<MaterialRegion>& regions,
                                std::map<RegionId,const Material*>& byRegion) {
    if (materials.empty() || regions.empty()) return fail<void>(ErrorCode::invalid_argument,"materials and material regions are required");
    std::map<MaterialId,const Material*> byId;
    for (const auto& m:materials) {
        if (!std::isfinite(m.density) || m.density<=0 ||
            (m.youngModulus && (!std::isfinite(*m.youngModulus)||*m.youngModulus<=0)) ||
            (m.poissonRatio && (!std::isfinite(*m.poissonRatio)||*m.poissonRatio<=-1||*m.poissonRatio>=0.5)) ||
            (m.yieldStress && (!std::isfinite(*m.yieldStress)||*m.yieldStress<0)) ||
            (m.referenceTemperatureKelvin && (!std::isfinite(*m.referenceTemperatureKelvin)||*m.referenceTemperatureKelvin<=0)) ||
            (m.referencePressurePascal && (!std::isfinite(*m.referencePressurePascal)||*m.referencePressurePascal<=0)))
            return fail<void>(ErrorCode::invalid_argument,"material properties are outside their physical range");
        if (!byId.emplace(m.id,&m).second) return fail<void>(ErrorCode::invalid_argument,"duplicate material ID");
    }
    for (const auto& region:regions) {
        auto found=byId.find(region.material);
        if (found==byId.end()) return fail<void>(ErrorCode::invalid_argument,"region references an unknown material");
        if (!byRegion.emplace(region.id,found->second).second) return fail<void>(ErrorCode::invalid_argument,"duplicate region ID");
    }
    return {};
}
Result<std::vector<Element>> validateMesh(TetrahedralMesh& mesh,SurfaceMesh& surface,
        const std::map<RegionId,const Material*>& byRegion,const MeshingOptions& options) {
    if (mesh.vertices.empty() || mesh.tetrahedra.empty())
        return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"volume mesh is empty");
    if (mesh.vertices.size()>options.maxVertices || mesh.tetrahedra.size()>options.maxTetrahedra ||
        mesh.vertices.size()>std::numeric_limits<std::uint32_t>::max() || mesh.tetrahedra.size()>std::numeric_limits<std::uint32_t>::max())
        return fail<std::vector<Element>>(ErrorCode::limit_exceeded,"volume mesh exceeds limits");
    for (auto p:mesh.vertices) if (!finite(p)) return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"non-finite mesh vertex");
    std::map<std::array<std::uint32_t,3>,Face> faces;
    std::set<std::array<std::uint32_t,4>> seen;
    std::vector<Element> elements; elements.reserve(mesh.tetrahedra.size());
    for (auto& tet:mesh.tetrahedra) {
        auto region=byRegion.find(tet.region);
        if (region==byRegion.end()) return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"tetrahedron references an unknown region");
        auto sorted=tet.vertices; std::sort(sorted.begin(),sorted.end());
        if (sorted.back()>=mesh.vertices.size() || std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end())
            return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"tetrahedron has invalid or repeated vertex indices");
        if (!seen.insert(sorted).second) return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"duplicate tetrahedron");
        auto six=signedSixVolume(mesh.vertices[tet.vertices[0]],mesh.vertices[tet.vertices[1]],
                                 mesh.vertices[tet.vertices[2]],mesh.vertices[tet.vertices[3]]);
        if (!six) return std::unexpected(six.error());
        if (*six<0) std::swap(tet.vertices[1],tet.vertices[2]);
        const long double volume=std::abs(*six)/6;
        const long double mass=volume*region->second->density;
        if (!(volume>0 && mass>0) || !representable(volume) || !representable(mass))
            return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"tetrahedron mass or volume is not representable");
        elements.push_back({volume,mass,region->second->id});
        auto [a,b,c,d]=tet.vertices;
        const std::array<std::array<std::uint32_t,3>,4> oriented{{{a,c,b},{a,b,d},{a,d,c},{b,c,d}}};
        for (auto face:oriented) {
            auto key=face; std::sort(key.begin(),key.end());
            auto [it,inserted]=faces.try_emplace(key,Face{face,tet.region,1});
            if (!inserted) {
                if (++it->second.count>2 || sameOrientation(it->second.oriented,face))
                    return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"inconsistent or nonmanifold shared tetrahedron face");
            }
        }
    }
    surface.vertices=mesh.vertices;
    for (const auto& [key,face]:faces) {
        (void)key;
        if (face.count==1) surface.triangles.push_back({face.oriented,face.region});
    }
    if (surface.triangles.empty()) return fail<std::vector<Element>>(ErrorCode::invalid_geometry,"volume mesh has no boundary");
    return elements;
}
Result<MassProperties> referenceMass(const TetrahedralMesh& mesh,const std::vector<Element>& elements) {
    long double totalVolume=0,totalMass=0;
    LVec first{};
    for (std::size_t i=0;i<elements.size();++i) {
        totalVolume+=elements[i].volume; totalMass+=elements[i].mass;
        first=add(first,mul(centroid(mesh,mesh.tetrahedra[i]),elements[i].mass));
    }
    if (!(totalMass>0) || !representable(totalVolume) || !representable(totalMass) ||
        !std::isfinite(first.x) || !std::isfinite(first.y) || !std::isfinite(first.z))
        return fail<MassProperties>(ErrorCode::invalid_geometry,"total mass is not representable");
    const auto cm=mul(first,1/totalMass);
    if (!representable(cm.x)||!representable(cm.y)||!representable(cm.z))
        return fail<MassProperties>(ErrorCode::invalid_geometry,"center of mass is not representable");
    long double moment[3][3]{};
    // Exact integral: E[x x^T] = (sum(v)sum(v)^T + sum(v v^T))/20
    // for a uniform tetrahedron, using vertices relative to global COM.
    for (std::size_t t=0;t<elements.size();++t) {
        long double p[4][3]{}, s[3]{};
        for (int i=0;i<4;++i) {
            auto q=sub(v(mesh.vertices[mesh.tetrahedra[t].vertices[i]]),cm);
            p[i][0]=q.x; p[i][1]=q.y; p[i][2]=q.z;
            for (int k=0;k<3;++k) s[k]+=p[i][k];
        }
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
            auto q=s[i]*s[j]; for (int k=0;k<4;++k) q+=p[k][i]*p[k][j];
            moment[i][j]+=elements[t].mass*q/20;
        }
    }
    MassProperties result; result.volume=double(totalVolume); result.mass=double(totalMass);
    result.centerOfMass={double(cm.x),double(cm.y),double(cm.z)};
    const auto trace=moment[0][0]+moment[1][1]+moment[2][2];
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        const auto value=(i==j?trace:0)-moment[i][j];
        if (!representable(value)) return fail<MassProperties>(ErrorCode::invalid_geometry,"inertia is not representable");
        result.inertia[i*3+j]=double(value);
    }
    return result;
}
// A particle represents a true partition cell: split a tetrahedron along
// its longest edge, allocating child volumes in proportion to their leaf counts.
// Only the O(log n) reconstruction path is retained, not n cell geometries.
struct CellNode { ParticleCell cell; std::size_t count; std::uint64_t key; };
std::uint64_t mix(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    return x^(x>>31);
}
CellNode rootCell(const TetrahedralMesh& mesh,std::size_t tetrahedron,std::size_t count,std::uint64_t seed) {
    ParticleCell cell;
    for (std::size_t i=0;i<4;++i) cell.vertices[i]=mesh.vertices[mesh.tetrahedra[tetrahedron].vertices[i]];
    return {cell,count,mix(seed^mix(static_cast<std::uint64_t>(tetrahedron)))};
}
std::pair<CellNode,CellNode> splitCell(const CellNode& parent) {
    constexpr std::array<std::array<unsigned,2>,6> edges{{{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}}};
    const auto start=static_cast<std::size_t>(parent.key%edges.size());
    auto edge=edges[start]; long double longest=-1;
    for (std::size_t i=0;i<edges.size();++i) {
        const auto e=edges[(start+i)%edges.size()];
        const auto len=length(sub(v(parent.cell.vertices[e[1]]),v(parent.cell.vertices[e[0]])));
        if (len>longest) { longest=len; edge=e; }
    }
    const std::size_t leftCount=parent.count/2, rightCount=parent.count-leftCount;
    const long double fraction=static_cast<long double>(leftCount)/static_cast<long double>(parent.count);
    const auto a=v(parent.cell.vertices[edge[0]]), b=v(parent.cell.vertices[edge[1]]);
    const auto p=add(a,mul(sub(b,a),fraction));
    const Vec3 mid{double(p.x),double(p.y),double(p.z)};
    auto left=parent.cell, right=parent.cell;
    left.vertices[edge[1]]=mid; right.vertices[edge[0]]=mid;
    return {{left,leftCount,mix(parent.key^0x1b873593ULL)},
            {right,rightCount,mix(parent.key^0x85ebca6bULL)}};
}
ParticleCell cellAt(const TetrahedralMesh& mesh,std::size_t tet,std::size_t perTet,
                    std::size_t local,std::uint64_t seed) {
    auto node=rootCell(mesh,tet,perTet,seed);
    while (node.count>1) {
        auto [left,right]=splitCell(node);
        if (local<left.count) node=left;
        else { local-=left.count; node=right; }
    }
    return node.cell;
}
Vec3 cellCentroid(const ParticleCell& cell) {
    const auto a=v(cell.vertices[0]); auto p=a;
    for (std::size_t i=1;i<4;++i) p=add(p,mul(sub(v(cell.vertices[i]),a),0.25L));
    return {double(p.x),double(p.y),double(p.z)};
}
Result<std::vector<MaterialParticle>> sample(const TetrahedralMesh& mesh,const std::vector<Element>& elements,
        const std::map<RegionId,const Material*>& materials,const SamplingOptions& options) {
    const auto n=options.particlesPerTetrahedron;
    if (!n) return fail<std::vector<MaterialParticle>>(ErrorCode::invalid_argument,"particles per tetrahedron must be positive");
    if (mesh.tetrahedra.size()>options.maxParticles/n)
        return fail<std::vector<MaterialParticle>>(ErrorCode::limit_exceeded,"particle count exceeds the limit");
    std::vector<MaterialParticle> particles; particles.reserve(mesh.tetrahedra.size()*n);
    std::vector<CellNode> stack;
    for (std::size_t t=0;t<mesh.tetrahedra.size();++t) {
        const auto& tet=mesh.tetrahedra[t];
        const auto expected=elements[t].volume/static_cast<long double>(n);
        long double volumeSum=0;
        stack.clear(); stack.push_back(rootCell(mesh,t,n,options.seed));
        while (!stack.empty()) {
            const auto node=stack.back(); stack.pop_back();
            if (node.count>1) {
                auto [left,right]=splitCell(node);
                stack.push_back(right); stack.push_back(left);
                continue;
            }
            const auto& c=node.cell.vertices;
            auto six=signedSixVolume(c[0],c[1],c[2],c[3]);
            if (!six) return std::unexpected(six.error());
            const long double actual=std::abs(*six)/6;
            if (std::abs(actual-expected)>expected*1e-9L)
                return fail<std::vector<MaterialParticle>>(ErrorCode::invalid_geometry,"particle cell volume lost precision");
            volumeSum+=actual;
            const double volume=double(actual);
            const double mass=volume*materials.at(tet.region)->density;
            if (!(volume>0 && mass>0) || !std::isfinite(volume) || !std::isfinite(mass))
                return fail<std::vector<MaterialParticle>>(ErrorCode::invalid_geometry,"particle mass or volume is not representable");
            particles.push_back({cellCentroid(node.cell),volume,mass,elements[t].material,tet.region,static_cast<std::uint32_t>(t)});
        }
        if (std::abs(volumeSum-elements[t].volume)>elements[t].volume*1e-9L)
            return fail<std::vector<MaterialParticle>>(ErrorCode::invalid_geometry,"particle cells do not conserve tetrahedron volume");
    }
    return particles;
}
Result<MassProperties> pointMass(std::span<const MaterialParticle> particles) {
    long double vol=0,mass=0; LVec first{};
    for (const auto& p:particles) { vol+=p.volume; mass+=p.mass; first=add(first,mul(v(p.position),p.mass)); }
    if (!(mass>0) || !representable(vol)||!representable(mass))
        return fail<MassProperties>(ErrorCode::invalid_geometry,"particle mass is not representable");
    const auto cm=mul(first,1/mass);
    if (!representable(cm.x)||!representable(cm.y)||!representable(cm.z))
        return fail<MassProperties>(ErrorCode::invalid_geometry,"particle center is not representable");
    long double inertia[3][3]{};
    for (const auto& p:particles) {
        const auto q=sub(v(p.position),cm); const long double a[]{q.x,q.y,q.z};
        const auto r2=dot(q,q);
        for (int i=0;i<3;++i) for (int j=0;j<3;++j)
            inertia[i][j]+=p.mass*((i==j?r2:0)-a[i]*a[j]);
    }
    MassProperties result; result.volume=double(vol); result.mass=double(mass);
    result.centerOfMass={double(cm.x),double(cm.y),double(cm.z)};
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        if (!representable(inertia[i][j])) return fail<MassProperties>(ErrorCode::invalid_geometry,"particle inertia is not representable");
        result.inertia[i*3+j]=double(inertia[i][j]);
    }
    return result;
}
bool valid(const ParticleDynamics& p) {
    return finite(p.position)&&finite(p.velocity)&&finite(p.force)&&std::isfinite(p.currentVolume)&&p.currentVolume>0&&
           finite(p.deformationGradient)&&finite(p.cauchyStress);
}
bool valid(const NodeDynamics& p) { return finite(p.position)&&finite(p.velocity)&&finite(p.force); }
}

struct VolumetricSolidMesh::Data {
    std::shared_ptr<const CadDesign> design;
    std::string id;
    std::uint64_t revision{};
    std::vector<Material> materials;
    std::vector<MaterialRegion> regions;
    TetrahedralMesh volume;
    SurfaceMesh surface;
    std::vector<MaterialParticle> particles;
    MassProperties referenceMass, particleMass;
    SamplingOptions sampling;
};
Result<VolumetricSolidMesh> VolumetricSolidMesh::create(std::shared_ptr<const CadDesign> design,const VolumeMesher& mesher,
        std::vector<Material> materials,std::vector<MaterialRegion> regions,MeshingOptions meshing,SamplingOptions sampling) {
    if (!design || design->id().empty()) return fail<VolumetricSolidMesh>(ErrorCode::invalid_argument,"immutable CAD design and identity are required");
    try {
        auto data=std::make_shared<Data>(); data->id=design->id(); data->revision=design->revision(); data->design=std::move(design);
        data->materials=std::move(materials); data->regions=std::move(regions); data->sampling=sampling;
        std::map<RegionId,const Material*> regionMap;
        if (auto status=validateProperties(data->materials,data->regions,regionMap); !status) return std::unexpected(status.error());
        auto meshed=mesher.mesh(*data->design,meshing);
        if (!meshed) return std::unexpected(meshed.error());
        data->volume=std::move(*meshed);
        auto elements=validateMesh(data->volume,data->surface,regionMap,meshing);
        if (!elements) return std::unexpected(elements.error());
        auto exact=referenceMass(data->volume,*elements);
        if (!exact) return std::unexpected(exact.error());
        auto particles=sample(data->volume,*elements,regionMap,sampling);
        if (!particles) return std::unexpected(particles.error());
        auto approximate=pointMass(*particles);
        if (!approximate) return std::unexpected(approximate.error());
        data->referenceMass=*exact; data->particleMass=*approximate; data->particles=std::move(*particles);
        return VolumetricSolidMesh(std::move(data));
    } catch (const std::bad_alloc&) { return fail<VolumetricSolidMesh>(ErrorCode::limit_exceeded,"solid allocation failed"); }
      catch (const std::exception& e) { return fail<VolumetricSolidMesh>(ErrorCode::backend_failure,e.what()); }
      catch (...) { return fail<VolumetricSolidMesh>(ErrorCode::backend_failure,"mesher or allocation failed"); }
}
Result<VolumetricSolidMesh> VolumetricSolidMesh::rebuild(std::shared_ptr<const CadDesign> design,const VolumeMesher& mesher,
        MeshingOptions options,SamplingOptions sampling) const {
    try { return create(std::move(design),mesher,data_->materials,data_->regions,options,sampling); }
    catch (const std::bad_alloc&) { return fail<VolumetricSolidMesh>(ErrorCode::limit_exceeded,"material snapshot allocation failed"); }
}
Result<VolumetricSolidMesh> VolumetricSolidMesh::withMaterials(
        std::vector<Material> materials,std::vector<MaterialRegion> regions) const {
    if(!data_)return fail<VolumetricSolidMesh>(ErrorCode::invalid_argument,"solid is empty or moved from");
    try {
        std::map<RegionId,const Material*> regionMap;
        if(auto status=validateProperties(materials,regions,regionMap);!status)return std::unexpected(status.error());
        std::vector<Element> elements;elements.reserve(data_->volume.tetrahedra.size());
        for(const auto& tet:data_->volume.tetrahedra) {
            const auto material=regionMap.find(tet.region);
            if(material==regionMap.end())return fail<VolumetricSolidMesh>(ErrorCode::invalid_argument,"material regions omit an existing tetrahedron");
            const auto& vertices=data_->volume.vertices;
            auto six=signedSixVolume(vertices[tet.vertices[0]],vertices[tet.vertices[1]],vertices[tet.vertices[2]],vertices[tet.vertices[3]]);
            if(!six)return std::unexpected(six.error());
            const long double volume=std::abs(*six)/6,mass=volume*material->second->density;
            if(!(volume>0&&mass>0)||!representable(mass)||!(double(mass)>0))
                return fail<VolumetricSolidMesh>(ErrorCode::invalid_geometry,"updated tetrahedron mass is not representable");
            elements.push_back({volume,mass,material->second->id});
        }
        auto exact=referenceMass(data_->volume,elements);if(!exact)return std::unexpected(exact.error());
        auto particles=data_->particles;
        for(auto& particle:particles) {
            const auto material=regionMap.find(particle.region);
            if(material==regionMap.end())return fail<VolumetricSolidMesh>(ErrorCode::invalid_argument,"material regions omit an existing particle");
            particle.mass=particle.volume*material->second->density;
            if(!(particle.mass>0)||!std::isfinite(particle.mass))
                return fail<VolumetricSolidMesh>(ErrorCode::invalid_geometry,"updated particle mass is not representable");
            particle.material=material->second->id;
        }
        auto points=pointMass(particles);if(!points)return std::unexpected(points.error());
        auto updated=std::make_shared<Data>();
        updated->design=data_->design;updated->id=data_->id;updated->revision=data_->revision;
        updated->volume=data_->volume;updated->surface=data_->surface;updated->sampling=data_->sampling;
        updated->materials=std::move(materials);updated->regions=std::move(regions);
        updated->particles=std::move(particles);updated->referenceMass=*exact;updated->particleMass=*points;
        return VolumetricSolidMesh(std::move(updated));
    } catch(const std::bad_alloc&) {return fail<VolumetricSolidMesh>(ErrorCode::limit_exceeded,"material snapshot allocation failed");}
      catch(const std::exception& e) {return fail<VolumetricSolidMesh>(ErrorCode::backend_failure,e.what());}
      catch(...) {return fail<VolumetricSolidMesh>(ErrorCode::backend_failure,"material update failed");}
}
const CadDesign& VolumetricSolidMesh::design() const noexcept { return *data_->design; }
std::string_view VolumetricSolidMesh::designId() const noexcept { return data_->id; }
std::uint64_t VolumetricSolidMesh::designRevision() const noexcept { return data_->revision; }
const TetrahedralMesh& VolumetricSolidMesh::volumeMesh() const noexcept { return data_->volume; }
const SurfaceMesh& VolumetricSolidMesh::surfaceMesh() const noexcept { return data_->surface; }
std::span<const Material> VolumetricSolidMesh::materials() const noexcept { return data_->materials; }
std::span<const MaterialRegion> VolumetricSolidMesh::regions() const noexcept { return data_->regions; }
std::span<const MaterialParticle> VolumetricSolidMesh::particles() const noexcept { return data_->particles; }
Result<ParticleCell> VolumetricSolidMesh::referenceCell(std::size_t index) const {
    if (index>=data_->particles.size()) return fail<ParticleCell>(ErrorCode::invalid_argument,"particle index is out of range");
    const auto n=data_->sampling.particlesPerTetrahedron;
    return cellAt(data_->volume,index/n,n,index%n,data_->sampling.seed);
}
MassProperties VolumetricSolidMesh::referenceMassProperties() const noexcept { return data_->referenceMass; }
MassProperties VolumetricSolidMesh::particleMassProperties() const noexcept { return data_->particleMass; }
ParticleSimulationState VolumetricSolidMesh::makeParticleState() const {
    ParticleSimulationState state; state.owner_=data_; state.values_.reserve(data_->particles.size());
    for (const auto& p:data_->particles) { ParticleDynamics value; value.position=p.position; value.currentVolume=p.volume; state.values_.push_back(value); }
    return state;
}
NodalSimulationState VolumetricSolidMesh::makeNodalState() const {
    NodalSimulationState state; state.owner_=data_; state.values_.reserve(data_->volume.vertices.size());
    for (auto p:data_->volume.vertices) { NodeDynamics value; value.position=p; state.values_.push_back(value); }
    return state;
}
Result<void> VolumetricSolidMesh::advance(ParticleSimulationState& state,const ParticleSolver& solver,double seconds) const {
    if (state.owner_.get()!=data_.get()) return fail<void>(ErrorCode::stale_state,"particle state belongs to another solid snapshot");
    if (!std::isfinite(seconds)||seconds<=0 || state.values_.size()!=data_->particles.size() ||
        !std::all_of(state.values_.begin(),state.values_.end(),[](const auto& p){return valid(p);}))
        return fail<void>(ErrorCode::invalid_argument,"invalid particle state or time step");
    try {
        auto candidate=state.values_;
        auto status=solver.step(data_->volume,data_->materials,data_->regions,data_->particles,candidate,seconds);
        if (!status) return status;
        if (!std::all_of(candidate.begin(),candidate.end(),[](const auto& p){return valid(p);}))
            return fail<void>(ErrorCode::backend_failure,"particle solver returned invalid state");
        std::copy(candidate.begin(),candidate.end(),state.values_.begin()); return {};
    } catch (const std::bad_alloc&) { return fail<void>(ErrorCode::limit_exceeded,"particle state allocation failed"); }
      catch (const std::exception& e) { return fail<void>(ErrorCode::backend_failure,e.what()); }
      catch (...) { return fail<void>(ErrorCode::backend_failure,"particle solver threw an exception"); }
}
Result<void> VolumetricSolidMesh::advance(NodalSimulationState& state,const FiniteElementSolver& solver,double seconds) const {
    if (state.owner_.get()!=data_.get()) return fail<void>(ErrorCode::stale_state,"node state belongs to another solid snapshot");
    if (!std::isfinite(seconds)||seconds<=0 || state.values_.size()!=data_->volume.vertices.size() ||
        !std::all_of(state.values_.begin(),state.values_.end(),[](const auto& p){return valid(p);}))
        return fail<void>(ErrorCode::invalid_argument,"invalid node state or time step");
    try {
        auto candidate=state.values_;
        auto status=solver.step(data_->volume,data_->materials,data_->regions,candidate,seconds);
        if (!status) return status;
        if (!std::all_of(candidate.begin(),candidate.end(),[](const auto& p){return valid(p);}))
            return fail<void>(ErrorCode::backend_failure,"finite element solver returned invalid state");
        std::copy(candidate.begin(),candidate.end(),state.values_.begin()); return {};
    } catch (const std::bad_alloc&) { return fail<void>(ErrorCode::limit_exceeded,"node state allocation failed"); }
      catch (const std::exception& e) { return fail<void>(ErrorCode::backend_failure,e.what()); }
      catch (...) { return fail<void>(ErrorCode::backend_failure,"finite element solver threw an exception"); }
}
}
