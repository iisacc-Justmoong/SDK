#include "Molecule.h"
#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace ii::pbr {
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string message) { return std::unexpected(Error{code,std::move(message)}); }
std::string formulaText(const std::map<std::string,std::uint64_t>& counts) {
    std::string text;
    auto add=[&](const auto& item){text+=item.first;if(item.second!=1)text+=std::to_string(item.second);};
    if(counts.contains("C")) {
        add(*counts.find("C"));if(counts.contains("H"))add(*counts.find("H"));
        for(const auto& item:counts)if(item.first!="C"&&item.first!="H")add(item);
    } else for(const auto& item:counts)add(item);
    return text;
}
bool matchesEndpoint(const BondEndpoint& endpoint,const Atom& atom) {
    switch(endpoint.kind) {
    case BondEndpointKind::any:return true;
    case BondEndpointKind::element:return !atom.custom&&atom.element==endpoint.id;
    case BondEndpointKind::customAtom:return atom.custom&&atom.custom->valid()&&atom.custom->definition().id==endpoint.id;
    }
    return false;
}
}
Result<Molecule> Molecule::create(std::vector<Atom> atoms,std::vector<Bond> bonds,std::string_view expected) {
    constexpr std::size_t maxAtoms=100000, maxBonds=200000;
    if(atoms.empty())return fail<Molecule>(ErrorCode::invalid_input,"a molecule requires at least one explicit atom");
    if(atoms.size()>maxAtoms||bonds.size()>maxBonds)return fail<Molecule>(ErrorCode::limit_exceeded,"molecule exceeds the atom or bond limit");
    try {
        std::map<std::string,std::uint64_t> counts;
        std::map<std::string,CustomAtomType,std::less<>> custom;
        bool hasIsotope=false;
        for(const auto& atom:atoms) {
            if(atom.custom) {
                if(!atom.custom->valid()||!atom.element.empty()||atom.formalCharge < -1000000||atom.formalCharge > 1000000)
                    return fail<Molecule>(ErrorCode::invalid_input,"invalid custom atom or charge");
                const auto& id=atom.custom->definition().id;
                auto [it,inserted]=custom.try_emplace(id,*atom.custom);
                if(!inserted&&!it->second.sameDefinition(*atom.custom))return fail<Molecule>(ErrorCode::invalid_input,"conflicting custom atom definitions");
                ++counts["{"+id+"}"];
            } else {
                if(!isKnownElementSymbol(atom.element)||atom.formalCharge < -16||atom.formalCharge > 16 ||
                   atom.isotopeMassNumber>400 || atom.implicitHydrogens>32 ||
                   (atom.element=="H" && atom.implicitHydrogens!=0))
                    return fail<Molecule>(ErrorCode::invalid_input,"invalid atom element, charge, isotope or implicit hydrogen count");
                ++counts[atom.element];
            }
            hasIsotope=hasIsotope||atom.isotopeMassNumber!=0;
            if(atom.implicitHydrogens)counts["H"]+=atom.implicitHydrogens;
        }
        std::set<std::pair<std::uint32_t,std::uint32_t>> pairs;
        std::map<std::string,CustomBondType,std::less<>> customBonds;
        for(const auto& bond:bonds) {
            if(bond.first>=atoms.size()||bond.second>=atoms.size()||bond.first==bond.second||
               (bond.stereo!=BondStereo::none&&bond.stereo!=BondStereo::solidWedge&&bond.stereo!=BondStereo::hashedWedge))
                return fail<Molecule>(ErrorCode::invalid_input,"invalid bond endpoints or order");
            if(bond.custom) {
                if(!bond.custom->valid()||bond.order!=BondOrder::custom)return fail<Molecule>(ErrorCode::invalid_input,"invalid custom bond type or order");
                const auto& d=bond.custom->definition();
                const auto direct=matchesEndpoint(d.first,atoms[bond.first])&&matchesEndpoint(d.second,atoms[bond.second]);
                const auto reverse=d.direction==BondDirection::undirected&&matchesEndpoint(d.first,atoms[bond.second])&&matchesEndpoint(d.second,atoms[bond.first]);
                if(!direct&&!reverse)return fail<Molecule>(ErrorCode::invalid_input,"custom bond endpoints do not match its definition");
                auto [it,inserted]=customBonds.try_emplace(d.id,*bond.custom);
                if(!inserted&&!it->second.sameDefinition(*bond.custom))return fail<Molecule>(ErrorCode::invalid_input,"conflicting custom bond definitions");
                if(customBonds.size()>4096)return fail<Molecule>(ErrorCode::limit_exceeded,"too many custom bond definitions");
            } else if((bond.order!=BondOrder::single&&bond.order!=BondOrder::doubleBond&&bond.order!=BondOrder::triple&&bond.order!=BondOrder::aromatic)||
                      (bond.stereo!=BondStereo::none&&bond.order!=BondOrder::single))
                return fail<Molecule>(ErrorCode::invalid_input,"invalid bond order or stereo");
            const auto pair=std::minmax(bond.first,bond.second);
            if(!pairs.emplace(pair.first,pair.second).second)return fail<Molecule>(ErrorCode::invalid_input,"duplicate bond");
        }
        std::vector<CustomAtomType> definitions;definitions.reserve(custom.size());
        for(const auto& [id,type]:custom){(void)id;definitions.push_back(type);}
        auto formula=parseMolecularFormula(formulaText(counts),definitions);if(!formula)return std::unexpected(formula.error());
        // The formula parser uses natural-isotope atomic weights; do not
        // present that approximation as the mass of an isotope-labelled graph.
        if(hasIsotope)formula->molarMassGramsPerMol.reset();
        if(!expected.empty()) {
            auto supplied=parseMolecularFormula(expected,definitions);if(!supplied)return std::unexpected(supplied.error());
            if(supplied->atoms!=counts)return fail<Molecule>(ErrorCode::invalid_input,"formula and structure have different atom counts");
        }
        Molecule result;result.formula_=std::move(*formula);result.atoms_=std::move(atoms);result.bonds_=std::move(bonds);result.customTypes_=std::move(definitions);
        result.customBondTypes_.reserve(customBonds.size());for(const auto& [id,type]:customBonds){(void)id;result.customBondTypes_.push_back(type);}return result;
    } catch(const std::bad_alloc&) { return fail<Molecule>(ErrorCode::limit_exceeded,"molecule allocation failed"); }
}
Result<Molecule> Molecule::create(std::vector<Atom> atoms,std::vector<Bond> bonds,std::string_view expected,
                                  std::span<const Molecule> isomers) {
    auto molecule=create(std::move(atoms),std::move(bonds),expected);
    if(!molecule || isomers.empty())return molecule;
    return molecule->withIsomers(isomers);
}
Result<Molecule> Molecule::withIsomers(std::span<const Molecule> isomers) const {
    constexpr std::size_t maxIsomers=4096,maxTotalAtoms=1000000,maxTotalBonds=2000000;
    if(atoms_.empty()||formula_.atoms.empty())return fail<Molecule>(ErrorCode::invalid_input,"base molecule is empty or moved from");
    if(isomers.size()>maxIsomers)return fail<Molecule>(ErrorCode::limit_exceeded,"too many isomer structures");
    std::size_t atoms=atoms_.size(),bonds=bonds_.size();
    for(const auto& item:isomers) {
        if(item.atoms_.empty()||item.formula_.atoms!=formula_.atoms)
            return fail<Molecule>(ErrorCode::invalid_input,"isomer and molecule have different elemental composition");
        if(item.customTypes_.size()!=customTypes_.size())return fail<Molecule>(ErrorCode::invalid_input,"isomer has different custom atom definitions");
        for(std::size_t i=0;i<customTypes_.size();++i)
            if(!customTypes_[i].sameDefinition(item.customTypes_[i]))return fail<Molecule>(ErrorCode::invalid_input,"isomer has conflicting custom atom definitions");
        if(item.atoms_.size()>maxTotalAtoms-atoms||item.bonds_.size()>maxTotalBonds-bonds)
            return fail<Molecule>(ErrorCode::limit_exceeded,"isomer structures exceed aggregate limits");
        atoms+=item.atoms_.size();bonds+=item.bonds_.size();
    }
    try {
        Molecule result;result.formula_=formula_;result.atoms_=atoms_;result.bonds_=bonds_;result.customTypes_=customTypes_;result.customBondTypes_=customBondTypes_;
        result.isomers_.reserve(isomers.size());
        for(const auto& item:isomers)result.isomers_.push_back({item.atoms_,item.bonds_});
        return result;
    } catch(const std::bad_alloc&) {return fail<Molecule>(ErrorCode::limit_exceeded,"isomer allocation failed");}
}
Result<Molecule> Molecule::isomer(std::size_t index) const {
    if(index>=isomers_.size())return fail<Molecule>(ErrorCode::invalid_input,"isomer index is out of range");
    try {return create(isomers_[index].atoms,isomers_[index].bonds,formula_.canonical);}
    catch(const std::bad_alloc&) {return fail<Molecule>(ErrorCode::limit_exceeded,"isomer allocation failed");}
}
Result<bool> Molecule::matchesFormula(std::string_view expected) const {
    if(atoms_.empty())return fail<bool>(ErrorCode::invalid_input,"molecule is empty or moved from");
    auto parsed=parseMolecularFormula(expected,customTypes_);if(!parsed)return std::unexpected(parsed.error());
    return parsed->atoms==formula_.atoms;
}
std::int64_t Molecule::totalFormalCharge() const noexcept {
    std::int64_t charge=0;for(const auto& atom:atoms_)charge+=atom.formalCharge;return charge;
}
}
