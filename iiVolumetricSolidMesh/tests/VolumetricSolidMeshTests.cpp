#include "BoxCad.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ii::volumetric;
namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (false)
bool near(double a, double b, double tolerance = 1e-10) {
    return std::abs(a-b) <= tolerance * std::max({1.0, std::abs(a), std::abs(b)});
}
Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double det(Vec3 a, Vec3 b, Vec3 c) { return dot(a,cross(b,c)); }
bool inside(const TetrahedralMesh& mesh, const MaterialParticle& p) {
    const auto& t=mesh.tetrahedra[p.tetrahedron];
    const auto a=mesh.vertices[t.vertices[0]], b=mesh.vertices[t.vertices[1]];
    const auto c=mesh.vertices[t.vertices[2]], d=mesh.vertices[t.vertices[3]];
    const auto u=sub(b,a), v=sub(c,a), w=sub(d,a), q=sub(p.position,a);
    const auto denom=det(u,v,w);
    const auto l1=det(q,v,w)/denom, l2=det(u,q,w)/denom, l3=det(u,v,q)/denom;
    const auto l0=1-l1-l2-l3;
    return std::min({l0,l1,l2,l3}) >= -1e-12 && std::max({l0,l1,l2,l3}) <= 1+1e-12;
}
Material material(MaterialId id, double density) { return {id, "test", density, {}, {}, {}}; }
struct FixedDesign final : CadDesign {
    std::string_view id() const noexcept override { return "external-cad"; }
    std::uint64_t revision() const noexcept override { return 9; }
};
struct FixedMesher final : VolumeMesher {
    TetrahedralMesh value;
    Result<TetrahedralMesh> mesh(const CadDesign&, const MeshingOptions&) const override { return value; }
};
struct MoveParticle final : ParticleSolver {
    bool fail = false, invalid = false;
    Result<void> step(const TetrahedralMesh&, std::span<const Material>, std::span<const MaterialRegion>,
        std::span<const MaterialParticle>, std::span<ParticleDynamics> state, double dt) const override {
        state[0].position.x += dt;
        if (invalid) state[0].currentVolume = -1;
        if (fail) return std::unexpected(Error{ErrorCode::backend_failure, "expected failure"});
        return {};
    }
};
struct MoveNode final : FiniteElementSolver {
    Result<void> step(const TetrahedralMesh&, std::span<const Material>, std::span<const MaterialRegion>,
        std::span<NodeDynamics> state, double dt) const override { state[0].position.y += dt; return {}; }
};
struct ThrowParticle final : ParticleSolver {
    Result<void> step(const TetrahedralMesh&, std::span<const Material>, std::span<const MaterialRegion>,
        std::span<const MaterialParticle>, std::span<ParticleDynamics> state, double) const override {
        state[0].position.x=999;
        throw std::runtime_error("solver failed");
    }
};
struct ThrowMesher final : VolumeMesher {
    Result<TetrahedralMesh> mesh(const CadDesign&,const MeshingOptions&) const override {
        throw std::runtime_error("mesher failed");
    }
};

void boxAndState() {
    auto design=BoxCad::create("asset-1", 1, {10,-2,5}, {2,3,4}, 7);
    CHECK(design); if (!design) return;
    BoxMesher mesher;
    MeshingOptions mesh; mesh.subdivisions={2,1,1};
    SamplingOptions sampling; sampling.particlesPerTetrahedron=5; sampling.seed=42;
    auto solid=VolumetricSolidMesh::create(*design,mesher,{material(4,1000)},{{7,4,"steel"}},mesh,sampling);
    CHECK(solid); if (!solid) { std::cerr << solid.error().message << '\n'; return; }
    CHECK(solid->geometryKind()==GeometryKind::mesh);
    CHECK(solid->designId()=="asset-1" && solid->designRevision()==1);
    CHECK(solid->volumeMesh().tetrahedra.size()==12);
    CHECK(solid->surfaceMesh().triangles.size()==20);
    CHECK(solid->particles().size()==60);
    const auto exact=solid->referenceMassProperties(), points=solid->particleMassProperties();
    CHECK(near(exact.volume,24) && near(exact.mass,24000));
    CHECK(near(exact.centerOfMass.x,11) && near(exact.centerOfMass.y,-0.5) && near(exact.centerOfMass.z,7));
    CHECK(near(exact.inertia[0],50000) && near(exact.inertia[4],40000) && near(exact.inertia[8],26000));
    CHECK(near(exact.inertia[1],0) && near(exact.inertia[2],0) && near(exact.inertia[5],0));
    CHECK(near(points.volume,24) && near(points.mass,24000));
    CHECK(near(points.centerOfMass.x,11) && near(points.centerOfMass.y,-0.5) && near(points.centerOfMass.z,7));
    for (std::size_t i=0;i<solid->particles().size();++i) {
        const auto& p=solid->particles()[i];
        CHECK(p.material==4 && p.region==7 && near(p.mass,1000*p.volume));
        CHECK(inside(solid->volumeMesh(),p));
        auto cell=solid->referenceCell(i); CHECK(cell);
        if (!cell) continue;
        const auto& v=cell->vertices;
        const auto volume=std::abs(det(sub(v[1],v[0]),sub(v[2],v[0]),sub(v[3],v[0])))/6;
        CHECK(near(volume,p.volume));
        CHECK(near((v[0].x+v[1].x+v[2].x+v[3].x)/4,p.position.x));
        CHECK(near((v[0].y+v[1].y+v[2].y+v[3].y)/4,p.position.y));
        CHECK(near((v[0].z+v[1].z+v[2].z+v[3].z)/4,p.position.z));
        for (auto vertex:v) { auto probe=p; probe.position=vertex; CHECK(inside(solid->volumeMesh(),probe)); }
    }
    CHECK(!solid->referenceCell(solid->particles().size()));
    for (const auto& face : solid->surfaceMesh().triangles) {
        const auto& vs=solid->surfaceMesh().vertices;
        const auto a=vs[face.vertices[0]], b=vs[face.vertices[1]], c=vs[face.vertices[2]];
        const Vec3 center{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
        CHECK(dot(cross(sub(b,a),sub(c,a)),sub(center,exact.centerOfMass))>0);
    }
    for (std::size_t n : {std::size_t(1),std::size_t(2),std::size_t(3),std::size_t(8),std::size_t(37),std::size_t(255)}) {
        sampling.particlesPerTetrahedron=n;
        auto resampled=solid->rebuild(*design,mesher,mesh,sampling);
        CHECK(resampled); if (!resampled) continue;
        CHECK(resampled->particles().size()==12*n);
        CHECK(near(resampled->particleMassProperties().mass,exact.mass));
        CHECK(near(resampled->particleMassProperties().centerOfMass.x,exact.centerOfMass.x));
        CHECK(near(resampled->particleMassProperties().centerOfMass.y,exact.centerOfMass.y));
        CHECK(near(resampled->particleMassProperties().centerOfMass.z,exact.centerOfMass.z));
        CHECK(near(resampled->referenceMassProperties().inertia[0],exact.inertia[0]));
    }
    auto ps=solid->makeParticleState(); auto ns=solid->makeNodalState();
    auto particleView=ps.particles(); auto nodeView=ns.nodes();
    const auto old=ps.particles()[0].position.x;
    MoveParticle fail; fail.fail=true;
    CHECK(!solid->advance(ps,fail,1)); CHECK(near(ps.particles()[0].position.x,old));
    MoveParticle invalid; invalid.invalid=true;
    CHECK(!solid->advance(ps,invalid,1)); CHECK(near(ps.particles()[0].position.x,old));
    ThrowParticle throws;
    CHECK(!solid->advance(ps,throws,1)); CHECK(near(ps.particles()[0].position.x,old));
    MoveParticle move; CHECK(solid->advance(ps,move,0.25));
    CHECK(near(ps.particles()[0].position.x,old+0.25));
    CHECK(particleView.data()==ps.particles().data() && near(particleView[0].position.x,old+0.25));
    CHECK(near(solid->particles()[0].position.x,old));
    CHECK(!solid->advance(ps,move,std::numeric_limits<double>::quiet_NaN()));
    MoveNode node; const auto oldY=ns.nodes()[0].position.y;
    CHECK(solid->advance(ns,node,0.5)); CHECK(near(ns.nodes()[0].position.y,oldY+0.5));
    CHECK(nodeView.data()==ns.nodes().data() && near(nodeView[0].position.y,oldY+0.5));
    CHECK(near(solid->volumeMesh().vertices[0].y,oldY));
    auto edited=BoxCad::create("asset-1",2,{10,-2,5},{4,3,4},7);
    CHECK(edited); if (!edited) return;
    auto changed=solid->rebuild(*edited,mesher,mesh,sampling);
    CHECK(changed); if (!changed) return;
    CHECK(changed->designRevision()==2 && solid->designRevision()==1);
    CHECK(near(changed->referenceMassProperties().mass,48000));
    CHECK(near(solid->referenceMassProperties().mass,24000));
    auto stale=changed->advance(ps,move,1);
    CHECK(!stale && stale.error().code==ErrorCode::stale_state);
}

void regionsAndValidation() {
    auto design=std::make_shared<FixedDesign>();
    FixedMesher mesher;
    mesher.value.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1},{3,0,0},{4,0,0},{3,1,0},{3,0,1}};
    mesher.value.tetrahedra={{{0,1,2,3},11},{{4,5,6,7},12}};
    std::vector<Material> materials{material(1,6),material(2,12)};
    std::vector<MaterialRegion> regions{{11,1,"first"},{12,2,"second"}};
    auto solid=VolumetricSolidMesh::create(design,mesher,materials,regions);
    CHECK(solid); if (!solid) return;
    auto props=solid->referenceMassProperties();
    CHECK(near(props.volume,1.0/3) && near(props.mass,3));
    CHECK(near(props.centerOfMass.x,2.25) && near(props.centerOfMass.y,0.25) && near(props.centerOfMass.z,0.25));
    CHECK(solid->surfaceMesh().triangles.size()==8);
    // A material-only edit preserves the mesh and sample cells while updating
    // both integrated and particle masses without calling a mesher.
    auto oldState=solid->makeParticleState();
    auto reassigned=solid->withMaterials({material(1,18),material(3,30)},{{11,1,"first"},{12,3,"second"}});
    CHECK(reassigned);
    if(reassigned) {
        auto mass=reassigned->referenceMassProperties(),points=reassigned->particleMassProperties();
        CHECK(near(mass.volume,1.0/3) && near(mass.mass,8) && near(points.mass,8));
        CHECK(near(mass.centerOfMass.x,2.125));
        auto fresh=VolumetricSolidMesh::create(design,mesher,{material(1,18),material(3,30)},{{11,1,"first"},{12,3,"second"}});
        CHECK(fresh);
        if(fresh) {
            const auto independent=fresh->referenceMassProperties();
            CHECK(near(independent.mass,mass.mass)&&near(independent.centerOfMass.x,mass.centerOfMass.x));
            for(std::size_t i=0;i<9;++i)CHECK(near(independent.inertia[i],mass.inertia[i]));
            CHECK(near(fresh->particleMassProperties().mass,points.mass));
        }
        CHECK(reassigned->surfaceMesh().triangles.size()==solid->surfaceMesh().triangles.size());
        CHECK(reassigned->volumeMesh().tetrahedra.size()==solid->volumeMesh().tetrahedra.size());
        CHECK(reassigned->particles().size()==solid->particles().size());
        for(std::size_t i=0;i<solid->particles().size();++i) {
            const auto& a=solid->particles()[i];const auto& b=reassigned->particles()[i];
            CHECK(near(a.position.x,b.position.x)&&near(a.position.y,b.position.y)&&near(a.position.z,b.position.z));
            CHECK(near(a.volume,b.volume)&&a.tetrahedron==b.tetrahedron&&a.region==b.region);
            CHECK(b.material==(b.region==11?1:3)&&near(b.mass,b.volume*(b.region==11?18:30)));
            auto oldCell=solid->referenceCell(i),newCell=reassigned->referenceCell(i);CHECK(oldCell&&newCell);
            if(oldCell&&newCell)for(std::size_t j=0;j<4;++j)CHECK(near(oldCell->vertices[j].x,newCell->vertices[j].x)&&near(oldCell->vertices[j].y,newCell->vertices[j].y)&&near(oldCell->vertices[j].z,newCell->vertices[j].z));
        }
        MoveParticle solver;auto stale=reassigned->advance(oldState,solver,0.1);
        CHECK(!stale&&stale.error().code==ErrorCode::stale_state);
        auto newState=reassigned->makeParticleState();CHECK(reassigned->advance(newState,solver,0.1));
    }
    CHECK(near(solid->referenceMassProperties().mass,3));
    CHECK(!solid->withMaterials({material(1,18)},{{11,1,"first"}}));
    CHECK(!solid->withMaterials({material(1,18),material(1,30)},regions));
    CHECK(!solid->withMaterials({material(1,0),material(2,12)},regions));
    CHECK(!solid->withMaterials({material(1,std::numeric_limits<double>::denorm_min()),material(2,12)},regions));
    CHECK(near(solid->referenceMassProperties().mass,3));
    // One unit tetrahedron of mass one: exact centroidal inertia has diagonal
    // 3/40 and off-diagonal 1/80, unlike the point-mass approximation.
    FixedMesher single; single.value=mesher.value;
    single.value.tetrahedra.resize(1);
    auto one=VolumetricSolidMesh::create(design,single,materials,regions);
    CHECK(one);
    if (one) {
        auto tensor=one->referenceMassProperties();
        CHECK(near(tensor.mass,1));
        CHECK(near(tensor.inertia[0],3.0/40) && near(tensor.inertia[4],3.0/40));
        CHECK(near(tensor.inertia[8],3.0/40) && near(tensor.inertia[1],1.0/80));
    }
    // Two different material regions share one face; it must not appear as an
    // exterior triangle, but it must retain both distinct masses.
    FixedMesher joined;
    joined.value.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,-1}};
    joined.value.tetrahedra={{{0,1,2,3},11},{{0,2,1,4},12}};
    auto two=VolumetricSolidMesh::create(design,joined,materials,regions);
    CHECK(two);
    if (two) {
        auto p=two->referenceMassProperties();
        CHECK(two->surfaceMesh().triangles.size()==6);
        CHECK(near(p.mass,3) && near(p.centerOfMass.z,-1.0/12));
    }
    auto missing=regions; missing.pop_back();
    CHECK(!VolumetricSolidMesh::create(design,mesher,materials,missing));
    auto duplicate=materials; duplicate.push_back(material(1,2));
    CHECK(!VolumetricSolidMesh::create(design,mesher,duplicate,regions));
    ThrowMesher throws;
    auto failed=VolumetricSolidMesh::create(design,throws,materials,regions);
    CHECK(!failed && failed.error().code==ErrorCode::backend_failure);
    auto bad=materials; bad[0].density=0;
    CHECK(!VolumetricSolidMesh::create(design,mesher,bad,regions));
    bad=materials; bad[0].poissonRatio=0.5;
    CHECK(!VolumetricSolidMesh::create(design,mesher,bad,regions));
    bad=materials; bad[0].referenceTemperatureKelvin=-1;
    CHECK(!VolumetricSolidMesh::create(design,mesher,bad,regions));
    bad=materials; bad[0].referencePressurePascal=0;
    CHECK(!VolumetricSolidMesh::create(design,mesher,bad,regions));
    auto invalid=mesher; invalid.value.tetrahedra[0].vertices[3]=0;
    CHECK(!VolumetricSolidMesh::create(design,invalid,materials,regions));
    invalid=mesher; invalid.value.vertices[0].x=std::numeric_limits<double>::quiet_NaN();
    CHECK(!VolumetricSolidMesh::create(design,invalid,materials,regions));
    invalid=mesher; invalid.value.tetrahedra.push_back(invalid.value.tetrahedra[0]);
    CHECK(!VolumetricSolidMesh::create(design,invalid,materials,regions));
    // Two cells on the same side of their common face are not conforming.
    invalid=joined; invalid.value.vertices[4]={0.1,0.1,1};
    CHECK(!VolumetricSolidMesh::create(design,invalid,materials,regions));
    invalid=joined; invalid.value.vertices.push_back({0,0,-2});
    invalid.value.tetrahedra.push_back({{0,2,1,5},12});
    CHECK(!VolumetricSolidMesh::create(design,invalid,materials,regions));
    SamplingOptions limit; limit.maxParticles=7;
    CHECK(!VolumetricSolidMesh::create(design,mesher,materials,regions,{},limit));
    limit={}; limit.particlesPerTetrahedron=0;
    CHECK(!VolumetricSolidMesh::create(design,mesher,materials,regions,{},limit));
    CHECK(!BoxCad::create("bad",1,{},{1,0,1},11));
    auto box=BoxCad::create("box",1,{},{1,1,1},11); BoxMesher boxMesher;
    CHECK(box); if (!box) return;
    MeshingOptions cap; cap.subdivisions={1000,1000,1000}; cap.maxTetrahedra=10;
    CHECK(!boxMesher.mesh(**box,cap));
}
}
int main() { boxAndState(); regionsAndValidation(); return failures ? 1 : 0; }
