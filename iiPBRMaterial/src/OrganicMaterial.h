#pragma once
#include "Molecule.h"
#include "PBRMaterial.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <utility>
#include <vector>

namespace ii::pbr {
// Values are caller-defined. Units and provenance accompany numeric values;
// no meaning, unit conversion, or biological effect is inferred from a key.
using OrganicValue=std::variant<bool,std::int64_t,double,std::string,std::vector<double>,std::vector<std::int64_t>,std::vector<std::string>>;
struct OrganicProperty { std::string key; OrganicValue value; std::string unit,source; };

// Fractions, when present, are relative to the immediate parent. Empty parent
// means the material itself. Omitted fractions are unknown, not zero.
struct OrganicComponent {
    std::string id,name,kind,parentId,molecularFormula;
    std::optional<Molecule> molecule;
    std::optional<double> massFraction,volumeFraction,moleFraction;
    std::vector<OrganicProperty> properties;
};
// Relations support cross-links beyond the acyclic composition hierarchy.
struct OrganicRelation { std::string from,to,type; std::vector<OrganicProperty> properties; };

struct OrganicOpticalProfile {
    std::array<double,3> absorptionPerMetre{},scatteringPerMetre{};
    double anisotropy{},refractiveIndex{1};
};
// Beer-Lambert attenuation of uncollided light only; not diffuse scattering
// or a subsurface renderer. RGB coefficients are in inverse metres.
[[nodiscard]] Result<std::array<double,3>> uncollidedTransmittance(const OrganicOpticalProfile&,double distanceMetres);

struct OrganicMaterialDefinition {
    MaterialRecord physical; // explicit density and provenance; may be a mixture
    SurfaceAppearance surface;
    std::optional<OrganicOpticalProfile> optics;
    std::vector<OrganicProperty> properties;
    std::vector<OrganicComponent> components;
    std::vector<OrganicRelation> relations;
    // Definitions needed by the bulk formula; component molecules can also
    // carry their own definitions. Equal IDs must have equal definitions.
    std::vector<CustomAtomType> customAtomTypes;
};
struct OrganicComponentState { std::string componentId; std::vector<OrganicProperty> properties; };
struct OrganicStateData {
    double timeSeconds{};
    // Environmental conditions do not silently replace measurement conditions.
    std::optional<double> temperatureKelvin,pressurePascal,oxygenPartialPressurePascal,pH;
    std::optional<double> hydrationMassFraction,viabilityFraction;
    // An explicit density override needs its own provenance. Reference
    // conditions are unknown unless supplied, even when the environment is set.
    std::optional<double> densityKgPerM3,densityReferenceTemperatureKelvin,densityReferencePressurePascal;
    std::string densitySource;
    std::optional<double> youngModulusPascal,poissonRatio,yieldStressPascal;
    std::vector<OrganicProperty> properties;
    std::vector<OrganicComponentState> components;
};
class OrganicState final {
public:
    [[nodiscard]] const OrganicStateData& data() const noexcept {return data_;}
private:
    std::shared_ptr<const OrganicMaterialDefinition> owner_;
    OrganicStateData data_;
    friend class OrganicMaterial;
};
// The application owns its model. step mutates a private draft; failed or
// invalid drafts never change the original state. Time is advanced by the SDK.
class OrganicStateModel {
public:
    virtual ~OrganicStateModel()=default;
    virtual Result<void> step(const OrganicMaterialDefinition&,OrganicStateData& draft,double seconds) const=0;
};
// Optional population-level approximation with caller-supplied parameters.
// Population must be a double-valued component-state property in [0, capacity].
struct LogisticPopulationParameters {
    std::string componentId,propertyKey;
    double growthRatePerSecond{},carryingCapacity{};
};
class LogisticPopulationModel final : public OrganicStateModel {
public:
    [[nodiscard]] static Result<LogisticPopulationModel> create(LogisticPopulationParameters);
    [[nodiscard]] Result<void> step(const OrganicMaterialDefinition&,OrganicStateData&,double seconds) const override;
private:
    explicit LogisticPopulationModel(LogisticPopulationParameters p):parameters_(std::move(p)){}
    LogisticPopulationParameters parameters_;
};
class OrganicMaterial final {
public:
    [[nodiscard]] static Result<OrganicMaterial> create(OrganicMaterialDefinition);
    [[nodiscard]] const OrganicMaterialDefinition& definition() const noexcept {return *definition_;}
    [[nodiscard]] Result<OrganicState> makeState(OrganicStateData={}) const;
    [[nodiscard]] Result<OrganicState> advance(const OrganicState&,const OrganicStateModel&,double seconds) const;
    [[nodiscard]] Result<ResolvedMaterial> resolve(const OrganicState&) const;
private:
    explicit OrganicMaterial(std::shared_ptr<const OrganicMaterialDefinition> definition):definition_(std::move(definition)){}
    std::shared_ptr<const OrganicMaterialDefinition> definition_;
};
}
