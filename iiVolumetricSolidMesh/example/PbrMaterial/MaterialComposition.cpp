#include "MaterialComposition.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <set>

namespace ii::material_app {
namespace {
template<class T> volumetric::Result<T> fail(volumetric::ErrorCode c,std::string s) {
    return std::unexpected(volumetric::Error{c,std::move(s)});
}
bool finite(volumetric::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
volumetric::Material makeMaterial(const pbr::ResolvedMaterial& resolved,volumetric::MaterialId id) {
    const auto& p=resolved.physical;
    volumetric::Material m;
    m.id=id;m.name=p.name;m.density=p.densityKgPerM3;
    m.youngModulus=p.youngModulusPascal;m.poissonRatio=p.poissonRatio;m.yieldStress=p.yieldStressPascal;
    m.sourceMaterialId=p.id;m.molecularFormula=resolved.molecular?resolved.molecular->canonical:std::string{};m.densitySource=p.densitySource;
    m.referenceTemperatureKelvin=p.temperatureKelvin;m.referencePressurePascal=p.pressurePascal;
    return m;
}
volumetric::Result<volumetric::VolumetricSolidMesh> applyResolved(
        const volumetric::VolumetricSolidMesh& solid,const pbr::ResolvedMaterial& resolved,
        volumetric::MaterialId materialId,volumetric::RegionId regionId) {
    std::vector<volumetric::Material> materials(solid.materials().begin(),solid.materials().end());
    std::vector<volumetric::MaterialRegion> regions(solid.regions().begin(),solid.regions().end());
    auto region=std::find_if(regions.begin(),regions.end(),[&](const auto& r){return r.id==regionId;});
    if(region==regions.end())return fail<volumetric::VolumetricSolidMesh>(volumetric::ErrorCode::invalid_argument,"unknown target material region");
    if(std::any_of(regions.begin(),regions.end(),[&](const auto& r){return r.id!=regionId&&r.material==materialId;}))
        return fail<volumetric::VolumetricSolidMesh>(volumetric::ErrorCode::invalid_argument,"material ID is already used by another region");
    auto material=std::find_if(materials.begin(),materials.end(),[&](const auto& m){return m.id==materialId;});
    auto replacement=makeMaterial(resolved,materialId);
    if(material==materials.end())materials.push_back(std::move(replacement));else *material=std::move(replacement);
    region->material=materialId;
    return solid.withMaterials(std::move(materials),std::move(regions));
}
}
volumetric::Result<MaterialSnapshots> resolveMaterials(const pbr::MaterialCatalog& catalog,std::span<const Binding> bindings) {
    if(bindings.empty())return fail<MaterialSnapshots>(volumetric::ErrorCode::invalid_argument,"material bindings are empty");
    try {
        MaterialSnapshots snapshots;std::set<std::string> ids;std::set<volumetric::MaterialId> materials;std::set<volumetric::RegionId> regions;
        for(const auto& b:bindings) {
            if(b.catalogId.empty()||!ids.insert(b.catalogId).second||
               !materials.insert(b.materialId).second||!regions.insert(b.regionId).second)
                return fail<MaterialSnapshots>(volumetric::ErrorCode::invalid_argument,"duplicate or empty material binding");
            auto resolved=b.molecularFormula.empty()?catalog.resolveId(b.catalogId):catalog.resolve(b.molecularFormula,b.customAtomTypes,b.catalogId);
            if(!resolved)return fail<MaterialSnapshots>(volumetric::ErrorCode::invalid_argument,resolved.error().message);
            const auto& p=resolved->physical;
            snapshots.materials.push_back(makeMaterial(*resolved,b.materialId)); snapshots.regions.push_back({b.regionId,b.materialId,p.name});
            snapshots.surfaces.push_back({b.materialId,b.surface});
        }
        return snapshots;
    } catch(const std::bad_alloc&) { return fail<MaterialSnapshots>(volumetric::ErrorCode::limit_exceeded,"material snapshot allocation failed"); }
}
volumetric::Result<AppliedIsomerMaterial> applyIsomerMaterial(
        const volumetric::VolumetricSolidMesh& solid,const pbr::MaterialCatalog& catalog,
        const pbr::Molecule& molecule,std::size_t index,const Binding& binding) {
    if(binding.catalogId.empty())return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::invalid_argument,"isomer material requires an explicit catalog ID");
    try {
        auto selected=molecule.isomer(index);
        if(!selected)return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::invalid_argument,selected.error().message);
        if(!binding.molecularFormula.empty()||!binding.customAtomTypes.empty()) {
            std::vector<pbr::CustomAtomType> types(selected->customAtomTypes().begin(),selected->customAtomTypes().end());
            types.insert(types.end(),binding.customAtomTypes.begin(),binding.customAtomTypes.end());
            const auto text=binding.molecularFormula.empty()?selected->formula().canonical:binding.molecularFormula;
            auto expected=pbr::parseMolecularFormula(text,types);
            if(!expected)return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::invalid_argument,expected.error().message);
            if(expected->atoms!=selected->formula().atoms)
                return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::invalid_argument,"binding formula and selected isomer have different elemental composition");
        }
        auto resolved=catalog.resolve(*selected,binding.catalogId);
        if(!resolved)return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::invalid_argument,resolved.error().message);
        auto updated=applyResolved(solid,*resolved,binding.materialId,binding.regionId);
        if(!updated)return std::unexpected(updated.error());
        return AppliedIsomerMaterial{std::move(*updated),std::move(*selected),index,binding.regionId,binding.surface};
    } catch(const std::bad_alloc&) {return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::limit_exceeded,"isomer material allocation failed");}
      catch(const std::exception& e) {return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::backend_failure,e.what());}
      catch(...) {return fail<AppliedIsomerMaterial>(volumetric::ErrorCode::backend_failure,"isomer material assignment failed");}
}
volumetric::Result<AppliedOrganicMaterial> applyOrganicMaterial(
        const volumetric::VolumetricSolidMesh& solid,const pbr::OrganicMaterial& material,
        const pbr::OrganicState& state,volumetric::MaterialId materialId,volumetric::RegionId regionId) {
    try {
        auto resolved=material.resolve(state);
        if(!resolved)return fail<AppliedOrganicMaterial>(volumetric::ErrorCode::invalid_argument,resolved.error().message);
        auto updated=applyResolved(solid,*resolved,materialId,regionId);
        if(!updated)return std::unexpected(updated.error());
        return AppliedOrganicMaterial{std::move(*updated),material,state,regionId,resolved->surface};
    } catch(const std::bad_alloc&) {return fail<AppliedOrganicMaterial>(volumetric::ErrorCode::limit_exceeded,"organic material allocation failed");}
      catch(const std::exception& e) {return fail<AppliedOrganicMaterial>(volumetric::ErrorCode::backend_failure,e.what());}
      catch(...) {return fail<AppliedOrganicMaterial>(volumetric::ErrorCode::backend_failure,"organic material assignment failed");}
}
pbr::Result<pbr::SurfaceSample> sampleAppearance(const MaterialSnapshots& snapshots,volumetric::RegionId region,
                                                 double u,double v,pbr::AddressMode mode) {
    const auto binding=std::find_if(snapshots.regions.begin(),snapshots.regions.end(),
                                    [&](const auto& x){return x.id==region;});
    if(binding==snapshots.regions.end())return std::unexpected(pbr::Error{pbr::ErrorCode::not_found,"unknown surface region"});
    const auto material=std::find_if(snapshots.surfaces.begin(),snapshots.surfaces.end(),
                                     [&](const auto& x){return x.materialId==binding->material;});
    if(material==snapshots.surfaces.end())return std::unexpected(pbr::Error{pbr::ErrorCode::not_found,"surface material is missing"});
    return pbr::sampleSurface(material->appearance,u,v,mode);
}
volumetric::Result<volumetric::TetrahedralMesh> TextureMappedMesher::mesh(
        const volumetric::CadDesign& design,const volumetric::MeshingOptions& options) const {
    if(!finite(projection_.origin)||!finite(projection_.uPerMetre)||!finite(projection_.vPerMetre))
        return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::invalid_argument,"non-finite texture projection");
    try {
        std::map<std::string,volumetric::RegionId,std::less<>> regions;
        for(const auto& b:bindings_) if(b.catalogId.empty()||!regions.emplace(b.catalogId,b.regionId).second)
            return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::invalid_argument,"duplicate or empty texture material binding");
        auto mesh=source_.mesh(design,options);if(!mesh)return std::unexpected(mesh.error());
        if(mesh->vertices.size()>options.maxVertices||mesh->tetrahedra.size()>options.maxTetrahedra)
            return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::limit_exceeded,"mesher output exceeds limits");
        for(auto& tet:mesh->tetrahedra) {
            long double x=0,y=0,z=0;
            for(auto index:tet.vertices) {
                if(index>=mesh->vertices.size()||!finite(mesh->vertices[index]))
                    return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::invalid_geometry,"invalid tetrahedron vertex");
                const auto p=mesh->vertices[index];x+=static_cast<long double>(p.x)/4;y+=static_cast<long double>(p.y)/4;z+=static_cast<long double>(p.z)/4;
            }
            x-=projection_.origin.x;y-=projection_.origin.y;z-=projection_.origin.z;
            const auto u=double(x*projection_.uPerMetre.x+y*projection_.uPerMetre.y+z*projection_.uPerMetre.z);
            const auto v=double(x*projection_.vPerMetre.x+y*projection_.vPerMetre.y+z*projection_.vPerMetre.z);
            auto id=map_.sample(u,v,projection_.address);
            if(!id)return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::invalid_argument,id.error().message);
            auto region=regions.find(*id);
            if(region==regions.end())return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::invalid_argument,"texture material has no region binding");
            tet.region=region->second;
        }
        return mesh;
    } catch(const std::bad_alloc&) {return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::limit_exceeded,"material map allocation failed");}
      catch(const std::exception& e) {return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::backend_failure,e.what());}
      catch(...) {return fail<volumetric::TetrahedralMesh>(volumetric::ErrorCode::backend_failure,"material mapping failed");}
}
}
