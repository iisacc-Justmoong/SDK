#pragma once
#include "VolumetricSolidMesh.h"

namespace ii::volumetric {
// A minimal parametric, dimensioned CAD source with immutable revisions.
class BoxCad final : public CadDesign {
public:
    [[nodiscard]] static Result<std::shared_ptr<const BoxCad>> create(
        std::string id, std::uint64_t revision, Vec3 origin, Vec3 dimensions, RegionId region);
    [[nodiscard]] std::string_view id() const noexcept override { return id_; }
    [[nodiscard]] std::uint64_t revision() const noexcept override { return revision_; }
    [[nodiscard]] Vec3 origin() const noexcept { return origin_; }
    [[nodiscard]] Vec3 dimensions() const noexcept { return dimensions_; }
    [[nodiscard]] RegionId region() const noexcept { return region_; }
private:
    BoxCad(std::string id, std::uint64_t revision, Vec3 origin, Vec3 dimensions, RegionId region)
        : id_(std::move(id)), revision_(revision), origin_(origin), dimensions_(dimensions), region_(region) {}
    std::string id_;
    std::uint64_t revision_;
    Vec3 origin_, dimensions_;
    RegionId region_;
};
class BoxMesher final : public VolumeMesher {
public:
    [[nodiscard]] Result<TetrahedralMesh> mesh(const CadDesign&, const MeshingOptions&) const override;
};
}
