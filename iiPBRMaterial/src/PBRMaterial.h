#pragma once
#include "CustomAtom.h"
#include "Texture.h"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::pbr {
class Molecule;
struct MaterialRecord {
    std::string id, name, formula, phase; // formula may be empty for mixtures
    double densityKgPerM3{};
    std::optional<double> temperatureKelvin, pressurePascal;
    std::optional<double> youngModulusPascal, poissonRatio, yieldStressPascal;
    std::string densitySource; // required provenance; reference conditions may be unknown
};
struct ResolvedMaterial {
    MaterialRecord physical;
    std::optional<MolecularFormula> molecular; // absent for mixtures without one formula
    SurfaceAppearance surface;
};
class MaterialCatalog {
public:
    // Limited NIST reference examples; density is not inferred from formula.
    [[nodiscard]] static Result<MaterialCatalog> referenceCatalog();
    [[nodiscard]] Result<void> add(MaterialRecord);
    [[nodiscard]] Result<void> add(MaterialRecord,std::span<const CustomAtomType>);
    [[nodiscard]] Result<std::vector<MaterialRecord>> candidates(std::string_view formula) const;
    [[nodiscard]] Result<std::vector<MaterialRecord>> candidates(std::string_view formula,std::span<const CustomAtomType>) const;
    // Formula-only lookup fails when multiple records share a formula. Select
    // an explicit material ID to resolve polymorphs, phases or reference data.
    [[nodiscard]] Result<ResolvedMaterial> resolve(std::string_view formula,
                                                   std::string_view materialId = {}) const;
    [[nodiscard]] Result<ResolvedMaterial> resolve(std::string_view formula,std::span<const CustomAtomType>,
                                                   std::string_view materialId) const;
    // Only the formula is compared; structure, phase and isomer identity are
    // not certified by this lookup. Select an appropriate material ID.
    [[nodiscard]] Result<ResolvedMaterial> resolve(const Molecule& molecule,
                                                   std::string_view materialId = {}) const;
    // Selects a stored alternative, then performs the same composition-based lookup.
    [[nodiscard]] Result<ResolvedMaterial> resolveIsomer(const Molecule&,std::size_t isomerIndex,
                                                         std::string_view materialId = {}) const;
    [[nodiscard]] Result<ResolvedMaterial> resolveId(std::string_view materialId) const;
private:
    struct Entry { MaterialRecord record; std::optional<MolecularFormula> formula; std::vector<CustomAtomType> customTypes; };
    [[nodiscard]] Result<ResolvedMaterial> resolveFormula(const MolecularFormula&,std::span<const CustomAtomType>,std::string_view) const;
    std::vector<Entry> entries_;
};
}
