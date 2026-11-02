#pragma once
#include "Molecule.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ii::pbr {
struct MapPoint { double x{},y{}; };
struct MapBounds { double minX{},minY{},maxX{},maxY{}; };
struct AtomGlyph { std::uint32_t atomIndex{}; MapPoint center; double radius{}; std::string label; std::string element; std::string customAtomId; };
struct BondStroke { std::uint32_t bondIndex{}; MapPoint from,to; bool dashed{}; std::string customBondId; bool arrowHead{}; };
struct BondWedge { std::uint32_t bondIndex{}; MapPoint tip,left,right; bool hashed{}; std::string customBondId; };
struct MapOptions {
    double bondLength{72},atomRadius{14},bondGap{4},padding{20};
    std::uint32_t layoutIterations{160};
};
class MolecularMap final {
public:
    [[nodiscard]] std::span<const AtomGlyph> atoms() const noexcept { return atoms_; }
    [[nodiscard]] std::span<const BondStroke> strokes() const noexcept { return strokes_; }
    [[nodiscard]] std::span<const BondWedge> wedges() const noexcept { return wedges_; }
    [[nodiscard]] MapBounds bounds() const noexcept { return bounds_; }
    [[nodiscard]] std::string_view formula() const noexcept { return formula_; }
private:
    MolecularMap() = default;
    std::vector<AtomGlyph> atoms_;
    std::vector<BondStroke> strokes_;
    std::vector<BondWedge> wedges_;
    MapBounds bounds_;
    std::string formula_;
    friend Result<MolecularMap> buildMolecularMap(const Molecule&,std::span<const MapPoint>,MapOptions);
};
// An empty positions span requests a deterministic schematic 2D layout.
// Otherwise supply exactly one finite 2D position per atom. Coordinates are
// diagram units, not a 3D conformer or measured bond lengths.
[[nodiscard]] Result<MolecularMap> buildMolecularMap(
    const Molecule&,std::span<const MapPoint> positions={},MapOptions options={});
// Index refers to a stored alternative, not the molecule's primary structure.
[[nodiscard]] Result<MolecularMap> buildIsomerMap(
    const Molecule&,std::size_t isomerIndex,std::span<const MapPoint> positions={},MapOptions options={});

class MolecularMapSink {
public:
    virtual ~MolecularMapSink()=default;
    virtual Result<void> stroke(const BondStroke&)=0;
    virtual Result<void> wedge(const BondWedge&)=0;
    virtual Result<void> atom(const AtomGlyph&)=0;
};
// Sends bonds first, then atoms, to an app-owned renderer such as iiGUI.
[[nodiscard]] Result<void> drawMolecularMap(const MolecularMap&,MolecularMapSink&);
struct Rgb8 { std::uint8_t r{},g{},b{}; };
struct MolecularSvgStyle {
    Rgb8 background{255,255,255},atomFill{255,255,255},text{25,25,25},line{50,50,50};
    double strokeWidth{2},fontSize{13};
};
// Returns in-memory SVG; the app/file provider controls saving and ownership.
[[nodiscard]] Result<std::string> molecularMapSvg(const MolecularMap&,MolecularSvgStyle style={});
}
