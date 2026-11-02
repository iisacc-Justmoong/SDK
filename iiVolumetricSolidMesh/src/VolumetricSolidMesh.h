#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ii::volumetric {

enum class ErrorCode { invalid_argument, invalid_geometry, limit_exceeded, backend_failure, stale_state };
struct Error { ErrorCode code; std::string message; };
template<class T> using Result = std::expected<T, Error>;

// All distances, volumes, masses, densities and stresses use SI units.
struct Vec3 { double x{}, y{}, z{}; };
using MaterialId = std::uint32_t;
using RegionId = std::uint32_t;
struct Material {
    MaterialId id{};
    std::string name;
    double density{}; // kg/m^3, finite and strictly positive
    std::optional<double> youngModulus; // Pa, positive when supplied
    std::optional<double> poissonRatio; // -1 < nu < 0.5
    std::optional<double> yieldStress;  // Pa, nonnegative when supplied
    // Immutable provenance copied from the app's material provider. These
    // values describe the snapshot used for mass, not a live SDK dependency.
    std::string sourceMaterialId;
    std::string molecularFormula;
    std::string densitySource;
    std::optional<double> referenceTemperatureKelvin, referencePressurePascal;
};
struct MaterialRegion { RegionId id{}; MaterialId material{}; std::string name; };
struct Tetrahedron { std::array<std::uint32_t, 4> vertices{}; RegionId region{}; };
struct TetrahedralMesh { std::vector<Vec3> vertices; std::vector<Tetrahedron> tetrahedra; };
struct SurfaceTriangle { std::array<std::uint32_t, 3> vertices{}; RegionId region{}; };
struct SurfaceMesh { std::vector<Vec3> vertices; std::vector<SurfaceTriangle> triangles; };
enum class GeometryKind { mesh };

// Each sample is a weighted material point, not an individual molecule.
// Position is in the immutable design/reference configuration.
struct MaterialParticle {
    Vec3 position;
    double volume{}, mass{};
    MaterialId material{};
    RegionId region{};
    std::uint32_t tetrahedron{};
};
// The disjoint reference sub-tetrahedron represented by one material point.
struct ParticleCell { std::array<Vec3, 4> vertices{}; };
struct MassProperties {
    double volume{}, mass{};
    Vec3 centerOfMass;
    // Row-major symmetric inertia tensor around centerOfMass, kg m^2.
    std::array<double, 9> inertia{};
};

// The application owns or wraps an immutable native CAD object. Neither this
// interface nor the mesher refers to another iiSDK module or file system.
class CadDesign {
public:
    virtual ~CadDesign() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual std::uint64_t revision() const noexcept = 0;
};
struct MeshingOptions {
    std::array<std::uint32_t, 3> subdivisions{1,1,1}; // native box mesher
    std::size_t maxVertices{500000};
    std::size_t maxTetrahedra{250000};
};
class VolumeMesher {
public:
    virtual ~VolumeMesher() = default;
    // Contract: a conforming, non-overlapping fill in metres. Shared faces
    // must share vertex indices. This module does not prove global coverage.
    virtual Result<TetrahedralMesh> mesh(const CadDesign&, const MeshingOptions&) const = 0;
};
struct SamplingOptions {
    std::size_t particlesPerTetrahedron{4};
    std::uint64_t seed{0};
    std::size_t maxParticles{1000000};
};

struct ParticleDynamics {
    Vec3 position, velocity, force;
    double currentVolume{};
    std::array<double, 9> deformationGradient{1,0,0, 0,1,0, 0,0,1};
    std::array<double, 9> cauchyStress{}; // Pa
};
struct NodeDynamics { Vec3 position, velocity, force; };
class ParticleSimulationState {
public:
    [[nodiscard]] std::span<ParticleDynamics> particles() noexcept { return values_; }
    [[nodiscard]] std::span<const ParticleDynamics> particles() const noexcept { return values_; }
private:
    std::shared_ptr<const void> owner_;
    std::vector<ParticleDynamics> values_;
    friend class VolumetricSolidMesh;
};
class NodalSimulationState {
public:
    [[nodiscard]] std::span<NodeDynamics> nodes() noexcept { return values_; }
    [[nodiscard]] std::span<const NodeDynamics> nodes() const noexcept { return values_; }
private:
    std::shared_ptr<const void> owner_;
    std::vector<NodeDynamics> values_;
    friend class VolumetricSolidMesh;
};
class ParticleSolver {
public:
    virtual ~ParticleSolver() = default;
    virtual Result<void> step(const TetrahedralMesh&, std::span<const Material>,
        std::span<const MaterialRegion>, std::span<const MaterialParticle>,
        std::span<ParticleDynamics>, double seconds) const = 0;
};
class FiniteElementSolver {
public:
    virtual ~FiniteElementSolver() = default;
    virtual Result<void> step(const TetrahedralMesh&, std::span<const Material>,
        std::span<const MaterialRegion>, std::span<NodeDynamics>, double seconds) const = 0;
};

// A value-semantic immutable design snapshot. Rebuild produces another object;
// simulation states are separate and cannot be applied to a different snapshot.
class VolumetricSolidMesh {
public:
    [[nodiscard]] static Result<VolumetricSolidMesh> create(
        std::shared_ptr<const CadDesign>, const VolumeMesher&,
        std::vector<Material>, std::vector<MaterialRegion>,
        MeshingOptions = {}, SamplingOptions = {});
    [[nodiscard]] Result<VolumetricSolidMesh> rebuild(
        std::shared_ptr<const CadDesign>, const VolumeMesher&,
        MeshingOptions, SamplingOptions) const;
    // Returns a new snapshot with updated region materials. Reuses the
    // existing tetrahedra and particle cells; old simulation states remain
    // attached to the original snapshot.
    [[nodiscard]] Result<VolumetricSolidMesh> withMaterials(
        std::vector<Material>, std::vector<MaterialRegion>) const;

    [[nodiscard]] const CadDesign& design() const noexcept;
    // Applications can classify the object as a mesh without depending on its
    // CAD kernel, and consume the indexed triangle surface separately.
    [[nodiscard]] constexpr GeometryKind geometryKind() const noexcept { return GeometryKind::mesh; }
    [[nodiscard]] std::string_view designId() const noexcept;
    [[nodiscard]] std::uint64_t designRevision() const noexcept;
    [[nodiscard]] const TetrahedralMesh& volumeMesh() const noexcept;
    [[nodiscard]] const SurfaceMesh& surfaceMesh() const noexcept;
    [[nodiscard]] std::span<const Material> materials() const noexcept;
    [[nodiscard]] std::span<const MaterialRegion> regions() const noexcept;
    [[nodiscard]] std::span<const MaterialParticle> particles() const noexcept;
    // Reconstructs the reference sub-tetrahedron on demand, in O(log n) for
    // n particles per tetrahedron. No per-cell geometry is stored in the object.
    [[nodiscard]] Result<ParticleCell> referenceCell(std::size_t particleIndex) const;
    // Exact for the validated piecewise-uniform tetrahedral reference mesh.
    [[nodiscard]] MassProperties referenceMassProperties() const noexcept;
    // Point-mass quadrature approximation; inertia need not equal the exact tensor.
    [[nodiscard]] MassProperties particleMassProperties() const noexcept;
    [[nodiscard]] ParticleSimulationState makeParticleState() const;
    [[nodiscard]] NodalSimulationState makeNodalState() const;
    // Transactional: errors, exceptions, or invalid outputs leave state intact.
    [[nodiscard]] Result<void> advance(ParticleSimulationState&, const ParticleSolver&, double seconds) const;
    [[nodiscard]] Result<void> advance(NodalSimulationState&, const FiniteElementSolver&, double seconds) const;
private:
    struct Data;
    explicit VolumetricSolidMesh(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
};
}
