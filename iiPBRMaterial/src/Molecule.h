#pragma once
#include "CustomAtom.h"
#include "CustomBond.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ii::pbr {
struct Atom {
    std::string element;
    std::uint16_t isotopeMassNumber{}; // zero means unspecified
    int formalCharge{};
    std::uint8_t implicitHydrogens{};
    // When present, element must be empty. Definition is an immutable value.
    std::optional<CustomAtomType> custom;
    [[nodiscard]] static Atom fromCustom(CustomAtomType type,int formalCharge=0,
                                         std::uint16_t isotopeMassNumber=0,std::uint8_t implicitHydrogens=0) {
        Atom atom;atom.custom=std::move(type);atom.formalCharge=formalCharge;
        atom.isotopeMassNumber=isotopeMassNumber;atom.implicitHydrogens=implicitHydrogens;return atom;
    }
};
enum class BondOrder : std::uint8_t { custom=0, single=1, doubleBond=2, triple=3, aromatic=4 };
enum class BondStereo : std::uint8_t { none, solidWedge, hashedWedge };
// Wedge direction goes from the first atom (narrow) to the second (wide).
struct Bond {
    std::uint32_t first{},second{};BondOrder order{BondOrder::single};BondStereo stereo{BondStereo::none};
    std::optional<CustomBondType> custom;
    [[nodiscard]] static Bond fromCustom(std::uint32_t first,std::uint32_t second,CustomBondType type,BondStereo stereo=BondStereo::none) {
        Bond bond;bond.first=first;bond.second=second;bond.order=BondOrder::custom;bond.stereo=stereo;bond.custom=std::move(type);return bond;
    }
};
// A stored alternative is a standalone atom/bond graph, without nested alternatives.
struct IsomerStructure { std::vector<Atom> atoms; std::vector<Bond> bonds; };

// Explicit connectivity is supplied by the application; a formula alone does
// not determine structure. Atom indices are stable within this immutable value.
class Molecule final {
public:
    [[nodiscard]] static Result<Molecule> create(std::vector<Atom>,std::vector<Bond>,
                                                 std::string_view expectedFormula={});
    [[nodiscard]] static Result<Molecule> create(std::vector<Atom>,std::vector<Bond>,
                                                 std::string_view expectedFormula,
                                                 std::span<const Molecule> isomers);
    // Produces a new molecule whose alternatives are the primary structures of
    // the supplied molecules. Replaces existing alternatives; element counts,
    // including implicit H, must match. Chemical isomerism is not validated.
    [[nodiscard]] Result<Molecule> withIsomers(std::span<const Molecule>) const;
    [[nodiscard]] std::span<const IsomerStructure> isomers() const noexcept { return isomers_; }
    // Selected alternative is returned as a standalone molecule.
    [[nodiscard]] Result<Molecule> isomer(std::size_t index) const;
    [[nodiscard]] const MolecularFormula& formula() const noexcept { return formula_; }
    [[nodiscard]] std::int64_t totalFormalCharge() const noexcept;
    [[nodiscard]] std::span<const Atom> atoms() const noexcept { return atoms_; }
    [[nodiscard]] std::span<const Bond> bonds() const noexcept { return bonds_; }
    [[nodiscard]] std::span<const CustomAtomType> customAtomTypes() const noexcept {return customTypes_;}
    [[nodiscard]] std::span<const CustomBondType> customBondTypes() const noexcept {return customBondTypes_;}
    [[nodiscard]] Result<bool> matchesFormula(std::string_view) const;
private:
    Molecule() = default;
    MolecularFormula formula_;
    std::vector<Atom> atoms_;
    std::vector<Bond> bonds_;
    std::vector<CustomAtomType> customTypes_;
    std::vector<CustomBondType> customBondTypes_;
    std::vector<IsomerStructure> isomers_;
};
}
