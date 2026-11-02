#pragma once
#include "MolecularFormula.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ii::pbr {
// Shared identifier syntax for user-defined atom and bond types.
[[nodiscard]] bool isValidCustomIdentifier(std::string_view) noexcept;
using CustomAtomValue=std::variant<bool,std::int64_t,double,std::string,std::vector<double>,std::vector<std::int64_t>,std::vector<std::string>>;
struct CustomAtomProperty {
    std::string key; CustomAtomValue value; std::string unit,source;
    bool operator==(const CustomAtomProperty&) const = default;
};
struct CustomAtomDefinition {
    // Identity is distinct from both display symbol and real element symbols.
    std::string id,symbol,name;
    std::optional<std::uint32_t> protons,neutrons,electrons;
    // Explicit mass is g/mol; absent means unknown. Zero may model a virtual site.
    std::optional<double> molarMassGramsPerMol,covalentRadiusMetres,vanDerWaalsRadiusMetres;
    std::vector<CustomAtomProperty> properties;
    bool operator==(const CustomAtomDefinition&) const = default;
};
class CustomAtomType final {
public:
    [[nodiscard]] static Result<CustomAtomType> create(CustomAtomDefinition);
    [[nodiscard]] bool valid() const noexcept {return bool(definition_);}
    [[nodiscard]] const CustomAtomDefinition& definition() const noexcept {return *definition_;}
    [[nodiscard]] bool sameDefinition(const CustomAtomType& other) const noexcept {
        return definition_==other.definition_||(definition_&&other.definition_&&*definition_==*other.definition_);
    }
private:
    explicit CustomAtomType(std::shared_ptr<const CustomAtomDefinition> d):definition_(std::move(d)){}
    std::shared_ptr<const CustomAtomDefinition> definition_;
};
}
