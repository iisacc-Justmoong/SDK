#pragma once
#include "CustomAtom.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ii::pbr {
enum class BondEndpointKind : std::uint8_t { any,element,customAtom };
struct BondEndpoint {
    BondEndpointKind kind{BondEndpointKind::any};
    std::string id; // chemical element symbol or custom atom ID; empty for any
    bool operator==(const BondEndpoint&) const = default;
};
enum class BondDirection : std::uint8_t { undirected,firstToSecond };
enum class CustomBondDisplay : std::uint8_t { solid,dashed,doubleLine,tripleLine };
using CustomBondValue=CustomAtomValue;
struct CustomBondProperty {
    std::string key;CustomBondValue value;std::string unit,source;
    bool operator==(const CustomBondProperty&) const = default;
};
struct CustomBondDefinition {
    std::string id,name,kind;
    BondDirection direction{BondDirection::undirected};
    CustomBondDisplay display{CustomBondDisplay::solid};
    BondEndpoint first,second;
    std::optional<double> order; // arbitrary, nonnegative, not inferred
    std::optional<double> dissociationEnergyJoulesPerMol;
    std::string energySource;
    std::optional<double> energyTemperatureKelvin,energyPressurePascal;
    std::optional<double> equilibriumLengthMetres,stiffnessNewtonsPerMetre,breakDistanceMetres;
    std::vector<CustomBondProperty> properties;
    bool operator==(const CustomBondDefinition&) const = default;
};
class CustomBondType final {
public:
    [[nodiscard]] static Result<CustomBondType> create(CustomBondDefinition);
    [[nodiscard]] bool valid() const noexcept {return bool(definition_);}
    [[nodiscard]] const CustomBondDefinition& definition() const noexcept {return *definition_;}
    [[nodiscard]] bool sameDefinition(const CustomBondType& other) const noexcept {
        return definition_==other.definition_||(definition_&&other.definition_&&*definition_==*other.definition_);
    }
private:
    explicit CustomBondType(std::shared_ptr<const CustomBondDefinition> d):definition_(std::move(d)){}
    std::shared_ptr<const CustomBondDefinition> definition_;
};

// Per individual bond. Radial force is on the second atom along the unit
// vector from first to second; negative values pull the atoms together.
struct BondPotentialSample { double energyJoules{},radialForceNewtons{}; };
class BondPotential {
public:
    virtual ~BondPotential()=default;
    virtual Result<BondPotentialSample> evaluate(const CustomBondDefinition&,double distanceMetres) const=0;
};
class HarmonicBondPotential final:public BondPotential {
public:
    [[nodiscard]] Result<BondPotentialSample> evaluate(const CustomBondDefinition&,double distanceMetres) const override;
};
// Validates the model output and converts exceptions into errors.
[[nodiscard]] Result<BondPotentialSample> evaluateBondPotential(const CustomBondType&,double distanceMetres,const BondPotential&);
}
