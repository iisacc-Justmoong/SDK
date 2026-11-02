#pragma once
// This is application composition code. Each SDK remains independently built.
#include <Molecule.h>
#include <OrganicMaterial.h>
#include <PBRMaterial.h>
#include <VolumetricSolidMesh.h>

namespace ii::material_app {
struct Binding {
    std::string catalogId;
    std::string molecularFormula; // empty selects an explicitly catalogued mixture by ID
    volumetric::MaterialId materialId{};
    volumetric::RegionId regionId{};
    pbr::SurfaceAppearance surface;
    std::vector<pbr::CustomAtomType> customAtomTypes;
};
struct SurfaceBinding { volumetric::MaterialId materialId{}; pbr::SurfaceAppearance appearance; };
struct MaterialSnapshots {
    std::vector<volumetric::Material> materials;
    std::vector<volumetric::MaterialRegion> regions;
    std::vector<SurfaceBinding> surfaces;
};
[[nodiscard]] volumetric::Result<MaterialSnapshots> resolveMaterials(
    const pbr::MaterialCatalog&,std::span<const Binding>);
// The application owns this association: the solid itself stores only the
// independent material record and region, not another SDK's molecule type.
struct AppliedIsomerMaterial {
    volumetric::VolumetricSolidMesh solid;
    pbr::Molecule isomer;
    std::size_t isomerIndex;
    volumetric::RegionId regionId;
    pbr::SurfaceAppearance surface;
};
// The catalog ID is mandatory. A material ID used by a different region is
// rejected so updating one region cannot silently change another region.
[[nodiscard]] volumetric::Result<AppliedIsomerMaterial> applyIsomerMaterial(
    const volumetric::VolumetricSolidMesh&,const pbr::MaterialCatalog&,
    const pbr::Molecule&,std::size_t isomerIndex,const Binding&);
// Keeps the organic definition and its matching state next to the updated
// solid. The app owns persistence and any biological/physical solvers.
struct AppliedOrganicMaterial {
    volumetric::VolumetricSolidMesh solid;
    pbr::OrganicMaterial material;
    pbr::OrganicState state;
    volumetric::RegionId regionId;
    pbr::SurfaceAppearance surface;
};
[[nodiscard]] volumetric::Result<AppliedOrganicMaterial> applyOrganicMaterial(
    const volumetric::VolumetricSolidMesh&,const pbr::OrganicMaterial&,
    const pbr::OrganicState&,volumetric::MaterialId,volumetric::RegionId);
// App renderer can resolve an external surface region to its PBR texture.
[[nodiscard]] pbr::Result<pbr::SurfaceSample> sampleAppearance(
    const MaterialSnapshots&,volumetric::RegionId,double u,double v,
    pbr::AddressMode mode=pbr::AddressMode::clamp);
struct PlanarProjection {
    volumetric::Vec3 origin{};
    // Dot(position - origin, axis) gives UV; axes have units 1/metre.
    volumetric::Vec3 uPerMetre{1,0,0},vPerMetre{0,1,0};
    pbr::AddressMode address{pbr::AddressMode::clamp};
};
// Explicit approximation: sample one categorical texture label per tetrahedron
// at its centroid. A material boundary crossing a tetrahedron is not split.
class TextureMappedMesher final : public volumetric::VolumeMesher {
public:
    TextureMappedMesher(const volumetric::VolumeMesher& source,const pbr::MaterialIdMap& map,
                        std::vector<Binding> bindings,PlanarProjection projection)
        : source_(source),map_(map),bindings_(std::move(bindings)),projection_(projection) {}
    [[nodiscard]] volumetric::Result<volumetric::TetrahedralMesh> mesh(
        const volumetric::CadDesign&,const volumetric::MeshingOptions&) const override;
private:
    const volumetric::VolumeMesher& source_;
    const pbr::MaterialIdMap& map_;
    std::vector<Binding> bindings_;
    PlanarProjection projection_;
};
}
