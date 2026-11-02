#include "PBRMaterial.h"
#include "Molecule.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace ii::pbr {
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string text) { return std::unexpected(Error{code,std::move(text)}); }
bool positive(const std::optional<double>& n) { return !n||(std::isfinite(*n)&&*n>0); }
bool matchingTypes(std::span<const CustomAtomType> expected,std::span<const CustomAtomType> actual) {
    for(const auto& e:expected) {
        const auto i=std::find_if(actual.begin(),actual.end(),[&](const auto& a){return a.valid()&&a.definition().id==e.definition().id;});
        if(i==actual.end()||!e.sameDefinition(*i))return false;
    }
    return true;
}
}
Result<MaterialCatalog> MaterialCatalog::referenceCatalog() {
    try {
    MaterialCatalog result;
    constexpr auto elements="https://physics.nist.gov/PhysRefData/XrayMassCoef/tab1.html";
    const MaterialRecord entries[]{
        {"nist:Fe","Iron","Fe","unspecified",7874,{},{},{},{},{},elements},
        {"nist:Al","Aluminium","Al","unspecified",2699,{},{},{},{},{},elements},
        {"nist:Cu","Copper","Cu","unspecified",8960,{},{},{},{},{},elements},
        {"nist:Si","Silicon","Si","unspecified",2330,{},{},{},{},{},elements},
        {"nist:C:graphite","Carbon, graphite","C","graphite",1700,{},{},{},{},{},elements},
        {"nist:H2O:liquid:20C","Pure water at 20 C","H2O","liquid",998.20675,293.15,{},{},{},{},"https://www.nist.gov/file/40736"}
    };
    for(const auto& item:entries) if(auto status=result.add(item);!status)return std::unexpected(status.error());
    return result;
    } catch(const std::bad_alloc&) { return fail<MaterialCatalog>(ErrorCode::limit_exceeded,"reference catalog allocation failed"); }
}
Result<void> MaterialCatalog::add(MaterialRecord record) {
    return add(std::move(record),{});
}
Result<void> MaterialCatalog::add(MaterialRecord record,std::span<const CustomAtomType> types) {
    if(record.id.empty()||record.name.empty()||record.densitySource.empty()||
       !std::isfinite(record.densityKgPerM3)||record.densityKgPerM3<=0||
       !positive(record.temperatureKelvin)||!positive(record.pressurePascal)||!positive(record.youngModulusPascal)||
       (record.poissonRatio&&(!std::isfinite(*record.poissonRatio)||*record.poissonRatio<=-1||*record.poissonRatio>=0.5))||
       (record.yieldStressPascal&&(!std::isfinite(*record.yieldStressPascal)||*record.yieldStressPascal<0)))
        return fail<void>(ErrorCode::invalid_input,"invalid material, reference conditions, or density provenance");
    if(std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.record.id==record.id;}))
        return fail<void>(ErrorCode::invalid_input,"duplicate material ID");
    if(types.size()>4096)return fail<void>(ErrorCode::limit_exceeded,"too many custom atom definitions");
    try {
        std::map<std::string,const CustomAtomType*,std::less<>> unique;
        for(const auto& type:types) {
            if(!type.valid())return fail<void>(ErrorCode::invalid_input,"empty custom atom definition");
            auto [it,inserted]=unique.emplace(type.definition().id,&type);
            if(!inserted&&!it->second->sameDefinition(type))return fail<void>(ErrorCode::invalid_input,"conflicting custom atom definitions");
        }
        std::optional<MolecularFormula> formula;
        if(!record.formula.empty()) {
            auto parsed=parseMolecularFormula(record.formula,types); if(!parsed)return std::unexpected(parsed.error());
            formula=std::move(*parsed);
        }
        std::vector<CustomAtomType> used;
        for(const auto& [id,type]:unique)if(formula&&formula->atoms.contains("{"+id+"}")) {
            for(const auto& entry:entries_)for(const auto& existing:entry.customTypes)
                if(existing.definition().id==id&&!existing.sameDefinition(*type))return fail<void>(ErrorCode::invalid_input,"catalog has a conflicting custom atom definition");
            used.push_back(*type);
        }
        entries_.push_back({std::move(record),std::move(formula),std::move(used)});return {};
    }
    catch(const std::bad_alloc&) { return fail<void>(ErrorCode::limit_exceeded,"material catalog allocation failed"); }
}
Result<std::vector<MaterialRecord>> MaterialCatalog::candidates(std::string_view text) const {
    return candidates(text,{});
}
Result<std::vector<MaterialRecord>> MaterialCatalog::candidates(std::string_view text,std::span<const CustomAtomType> types) const {
    auto formula=parseMolecularFormula(text,types); if(!formula)return std::unexpected(formula.error());
    try {
        std::vector<MaterialRecord> result;
        for(const auto& e:entries_)if(e.formula&&e.formula->atoms==formula->atoms) {
            if(!matchingTypes(e.customTypes,types))return fail<std::vector<MaterialRecord>>(ErrorCode::invalid_input,"conflicting custom atom definition");
            result.push_back(e.record);
        }
        return result;
    } catch(const std::bad_alloc&) { return fail<std::vector<MaterialRecord>>(ErrorCode::limit_exceeded,"material lookup allocation failed"); }
}
Result<ResolvedMaterial> MaterialCatalog::resolve(std::string_view text,std::string_view id) const {
    return resolve(text,{},id);
}
Result<ResolvedMaterial> MaterialCatalog::resolve(std::string_view text,std::span<const CustomAtomType> types,std::string_view id) const {
    auto formula=parseMolecularFormula(text,types);if(!formula)return std::unexpected(formula.error());
    return resolveFormula(*formula,types,id);
}
Result<ResolvedMaterial> MaterialCatalog::resolveFormula(const MolecularFormula& formula,std::span<const CustomAtomType> types,std::string_view id) const {
    const Entry* found=nullptr;
    for(const auto& e:entries_) if(e.formula&&e.formula->atoms==formula.atoms && (id.empty()||id==e.record.id)) {
        if(!matchingTypes(e.customTypes,types))return fail<ResolvedMaterial>(ErrorCode::invalid_input,"conflicting custom atom definition");
        if(found)return fail<ResolvedMaterial>(ErrorCode::ambiguous,"multiple materials share this formula; select a material ID");
        found=&e;
    }
    if(!found)return fail<ResolvedMaterial>(ErrorCode::not_found,"no material matches this formula and ID");
    try { return ResolvedMaterial{found->record,formula,{}}; }
    catch(const std::bad_alloc&) { return fail<ResolvedMaterial>(ErrorCode::limit_exceeded,"material lookup allocation failed"); }
}
Result<ResolvedMaterial> MaterialCatalog::resolveId(std::string_view id) const {
    auto i=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.record.id==id;});
    if(i==entries_.end())return fail<ResolvedMaterial>(ErrorCode::not_found,"unknown material ID");
    try { return ResolvedMaterial{i->record,i->formula,{}}; }
    catch(const std::bad_alloc&) { return fail<ResolvedMaterial>(ErrorCode::limit_exceeded,"material lookup allocation failed"); }
}
Result<ResolvedMaterial> MaterialCatalog::resolve(const Molecule& molecule,std::string_view id) const {
    if(molecule.atoms().empty())return fail<ResolvedMaterial>(ErrorCode::invalid_input,"molecule is empty or moved from");
    return resolveFormula(molecule.formula(),molecule.customAtomTypes(),id);
}
Result<ResolvedMaterial> MaterialCatalog::resolveIsomer(const Molecule& molecule,std::size_t index,std::string_view id) const {
    auto selected=molecule.isomer(index);if(!selected)return std::unexpected(selected.error());
    return resolve(*selected,id);
}
}
