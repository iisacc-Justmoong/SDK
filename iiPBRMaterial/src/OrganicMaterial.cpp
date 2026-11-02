#include "OrganicMaterial.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

namespace ii::pbr {
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string message) {return std::unexpected(Error{code,std::move(message)});}
constexpr std::size_t maxNodes=4096,maxRelations=16384,maxProperties=4096,maxValues=1000000,maxText=4*1024*1024;
bool validText(const std::string& s,bool required=false) {return s.size()<=maxText&&(!required||!s.empty());}
bool fraction(const std::optional<double>& v) {return !v||(std::isfinite(*v)&&*v>=0&&*v<=1);}
bool positive(const std::optional<double>& v) {return !v||(std::isfinite(*v)&&*v>0);}
bool nonnegative(const std::optional<double>& v) {return !v||(std::isfinite(*v)&&*v>=0);}
Result<void> validateProperties(std::span<const OrganicProperty> properties) {
    if(properties.size()>maxProperties)return fail<void>(ErrorCode::limit_exceeded,"too many organic properties in one scope");
    std::set<std::string,std::less<>> keys;
    for(const auto& p:properties) {
        if(!validText(p.key,true)||!validText(p.unit)||!validText(p.source)||!keys.insert(p.key).second)
            return fail<void>(ErrorCode::invalid_input,"invalid or duplicate organic property key");
        const bool valid=std::visit([](const auto& value) {
            using V=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<V,double>)return std::isfinite(value);
            else if constexpr(std::is_same_v<V,std::string>)return validText(value);
            else if constexpr(std::is_same_v<V,std::vector<double>>)
                return value.size()<=maxValues&&std::all_of(value.begin(),value.end(),[](double x){return std::isfinite(x);});
            else if constexpr(std::is_same_v<V,std::vector<std::int64_t>>)return value.size()<=maxValues;
            else if constexpr(std::is_same_v<V,std::vector<std::string>>)
                return value.size()<=maxValues&&std::all_of(value.begin(),value.end(),[](const auto& x){return validText(x);});
            else return true;
        },p.value);
        if(!valid)return fail<void>(ErrorCode::invalid_input,"invalid organic property value");
    }
    return {};
}
Result<void> validateOptics(const OrganicOpticalProfile& profile) {
    if(!std::isfinite(profile.anisotropy)||profile.anisotropy < -1||profile.anisotropy > 1||
       !std::isfinite(profile.refractiveIndex)||profile.refractiveIndex<=0)
        return fail<void>(ErrorCode::invalid_input,"invalid optical anisotropy or refractive index");
    for(std::size_t i=0;i<3;++i)
        if(!std::isfinite(profile.absorptionPerMetre[i])||profile.absorptionPerMetre[i]<0||
           !std::isfinite(profile.scatteringPerMetre[i])||profile.scatteringPerMetre[i]<0)
            return fail<void>(ErrorCode::invalid_input,"invalid optical attenuation coefficients");
    return {};
}
Result<void> validateDefinition(const OrganicMaterialDefinition& d) {
    if(d.components.size()>maxNodes||d.relations.size()>maxRelations)
        return fail<void>(ErrorCode::limit_exceeded,"organic structure exceeds limits");
    MaterialCatalog catalog;if(auto status=catalog.add(d.physical,d.customAtomTypes);!status)return status;
    auto surface=sampleSurface(d.surface,0.5,0.5);if(!surface)return std::unexpected(surface.error());
    if(d.optics)if(auto status=validateOptics(*d.optics);!status)return status;
    if(auto status=validateProperties(d.properties);!status)return status;
    std::map<std::string,CustomAtomType,std::less<>> custom;
    auto collect=[&](std::span<const CustomAtomType> types)->Result<void> {
        for(const auto& type:types) {
            if(!type.valid())return fail<void>(ErrorCode::invalid_input,"empty custom atom definition");
            auto [it,inserted]=custom.try_emplace(type.definition().id,type);
            if(!inserted&&!it->second.sameDefinition(type))return fail<void>(ErrorCode::invalid_input,"conflicting organic custom atom definitions");
        }
        if(custom.size()>4096)return fail<void>(ErrorCode::limit_exceeded,"too many organic custom atom definitions");
        return {};
    };
    if(auto status=collect(d.customAtomTypes);!status)return status;
    for(const auto& c:d.components)if(c.molecule)if(auto status=collect(c.molecule->customAtomTypes());!status)return status;
    std::vector<CustomAtomType> allTypes;allTypes.reserve(custom.size());
    for(const auto& [id,type]:custom){(void)id;allTypes.push_back(type);}
    std::map<std::string,std::size_t,std::less<>> indices;
    for(std::size_t i=0;i<d.components.size();++i) {
        const auto& c=d.components[i];
        if(!validText(c.id,true)||!validText(c.name,true)||!validText(c.kind,true)||!validText(c.parentId)||!validText(c.molecularFormula)||!indices.emplace(c.id,i).second)
            return fail<void>(ErrorCode::invalid_input,"invalid or duplicate organic component");
        if(!fraction(c.massFraction)||!fraction(c.volumeFraction)||!fraction(c.moleFraction))
            return fail<void>(ErrorCode::invalid_input,"component fractions must be between zero and one");
        if(auto status=validateProperties(c.properties);!status)return status;
        if(c.molecule&&c.molecule->atoms().empty())return fail<void>(ErrorCode::invalid_input,"component molecule is empty");
        if(!c.molecularFormula.empty()) {
            auto parsed=parseMolecularFormula(c.molecularFormula,allTypes);if(!parsed)return std::unexpected(parsed.error());
            if(c.molecule&&parsed->atoms!=c.molecule->formula().atoms)
                return fail<void>(ErrorCode::invalid_input,"component formula and molecule do not match");
        }
    }
    std::map<std::string,std::array<long double,3>,std::less<>> totals;
    for(const auto& c:d.components) {
        if(!c.parentId.empty()&&!indices.contains(c.parentId))return fail<void>(ErrorCode::invalid_input,"unknown component parent");
        auto& total=totals[c.parentId];
        if(c.massFraction)total[0]+=*c.massFraction;
        if(c.volumeFraction)total[1]+=*c.volumeFraction;
        if(c.moleFraction)total[2]+=*c.moleFraction;
        if(std::any_of(total.begin(),total.end(),[](long double x){return x>1.0L+1e-9L;}))
            return fail<void>(ErrorCode::invalid_input,"component fractions exceed their parent");
    }
    std::vector<unsigned char> visited(d.components.size());
    for(std::size_t i=0;i<d.components.size();++i) {
        std::vector<std::size_t> path;auto current=i;
        while(current<d.components.size()&&!visited[current]) {
            visited[current]=1;path.push_back(current);
            const auto& parent=d.components[current].parentId;
            current=parent.empty()?d.components.size():indices.at(parent);
        }
        if(current<d.components.size()&&visited[current]==1)return fail<void>(ErrorCode::invalid_input,"component hierarchy contains a cycle");
        for(auto index:path)visited[index]=2;
    }
    for(const auto& relation:d.relations) {
        if(!validText(relation.type,true)||!indices.contains(relation.from)||!indices.contains(relation.to))
            return fail<void>(ErrorCode::invalid_input,"invalid organic relationship");
        if(auto status=validateProperties(relation.properties);!status)return status;
    }
    return {};
}
MaterialRecord physicalForState(const OrganicMaterialDefinition& d,const OrganicStateData& s) {
    auto physical=d.physical;
    if(s.densityKgPerM3) {
        physical.densityKgPerM3=*s.densityKgPerM3;physical.densitySource=s.densitySource;
        physical.temperatureKelvin=s.densityReferenceTemperatureKelvin;
        physical.pressurePascal=s.densityReferencePressurePascal;
    }
    if(s.youngModulusPascal)physical.youngModulusPascal=s.youngModulusPascal;
    if(s.poissonRatio)physical.poissonRatio=s.poissonRatio;
    if(s.yieldStressPascal)physical.yieldStressPascal=s.yieldStressPascal;
    return physical;
}
Result<void> validateState(const OrganicMaterialDefinition& definition,const OrganicStateData& s) {
    if(!std::isfinite(s.timeSeconds)||s.timeSeconds<0||!positive(s.temperatureKelvin)||!nonnegative(s.pressurePascal)||
       !nonnegative(s.oxygenPartialPressurePascal)||(s.pH&&!std::isfinite(*s.pH))||
       !fraction(s.hydrationMassFraction)||!fraction(s.viabilityFraction)||!positive(s.densityKgPerM3)||
       !validText(s.densitySource)||(!s.densityKgPerM3&&(!s.densitySource.empty()||s.densityReferenceTemperatureKelvin||s.densityReferencePressurePascal))||
       (s.densityKgPerM3&&s.densitySource.empty()))
        return fail<void>(ErrorCode::invalid_input,"invalid organic state or density provenance");
    if(auto status=validateProperties(s.properties);!status)return status;
    if(s.components.size()>maxNodes)return fail<void>(ErrorCode::limit_exceeded,"too many organic component states");
    std::set<std::string,std::less<>> componentIds,seen;
    for(const auto& c:definition.components)componentIds.insert(c.id);
    for(const auto& c:s.components) {
        if(!componentIds.contains(c.componentId)||!seen.insert(c.componentId).second)
            return fail<void>(ErrorCode::invalid_input,"unknown or duplicate organic component state");
        if(auto status=validateProperties(c.properties);!status)return status;
    }
    MaterialCatalog catalog;return catalog.add(physicalForState(definition,s),definition.customAtomTypes);
}
}
Result<std::array<double,3>> uncollidedTransmittance(const OrganicOpticalProfile& profile,double distance) {
    if(auto status=validateOptics(profile);!status)return std::unexpected(status.error());
    if(!std::isfinite(distance)||distance<0)return fail<std::array<double,3>>(ErrorCode::invalid_input,"invalid optical distance");
    std::array<double,3> result{};
    for(std::size_t i=0;i<3;++i) {
        const auto distanceLong=static_cast<long double>(distance);
        const auto attenuation=static_cast<long double>(profile.absorptionPerMetre[i])*distanceLong+
                               static_cast<long double>(profile.scatteringPerMetre[i])*distanceLong;
        result[i]=double(std::exp(-attenuation));
    }
    return result;
}
Result<LogisticPopulationModel> LogisticPopulationModel::create(LogisticPopulationParameters p) {
    if(!validText(p.componentId,true)||!validText(p.propertyKey,true)||!std::isfinite(p.growthRatePerSecond)||p.growthRatePerSecond<0||
       !std::isfinite(p.carryingCapacity)||p.carryingCapacity<=0)
        return fail<LogisticPopulationModel>(ErrorCode::invalid_input,"invalid logistic population parameters");
    return LogisticPopulationModel(std::move(p));
}
Result<void> LogisticPopulationModel::step(const OrganicMaterialDefinition&,OrganicStateData& state,double seconds) const {
    if(!std::isfinite(seconds)||seconds<=0)return fail<void>(ErrorCode::invalid_input,"time step must be positive and finite");
    try {
        auto component=std::find_if(state.components.begin(),state.components.end(),[&](const auto& c){return c.componentId==parameters_.componentId;});
        if(component==state.components.end())return fail<void>(ErrorCode::not_found,"population component state not found");
        auto property=std::find_if(component->properties.begin(),component->properties.end(),[&](const auto& p){return p.key==parameters_.propertyKey;});
        if(property==component->properties.end())return fail<void>(ErrorCode::not_found,"population property not found");
        auto population=std::get_if<double>(&property->value);
        if(!population||!std::isfinite(*population)||*population<0||*population>parameters_.carryingCapacity)
            return fail<void>(ErrorCode::invalid_input,"population must be a double between zero and carrying capacity");
        double next=*population;
        if(next>0&&next<parameters_.carryingCapacity&&parameters_.growthRatePerSecond>0) {
            const double logOdds=std::log(parameters_.carryingCapacity-next)-std::log(next)-parameters_.growthRatePerSecond*seconds;
            if(logOdds < -36) next=parameters_.carryingCapacity;
            else {
                const double softplus=logOdds>0?logOdds+std::log1p(std::exp(-logOdds)):std::log1p(std::exp(logOdds));
                const double value=std::exp(std::log(parameters_.carryingCapacity)-softplus);
                next=std::clamp(value,next,parameters_.carryingCapacity);
            }
        }
        std::string source="LogisticPopulationModel";
        *population=next;property->source=std::move(source);return {};
    } catch(const std::bad_alloc&) {return fail<void>(ErrorCode::limit_exceeded,"population model allocation failed");}
}
Result<OrganicMaterial> OrganicMaterial::create(OrganicMaterialDefinition d) {
    try {
        if(auto status=validateDefinition(d);!status)return std::unexpected(status.error());
        return OrganicMaterial(std::make_shared<const OrganicMaterialDefinition>(std::move(d)));
    } catch(const std::bad_alloc&) {return fail<OrganicMaterial>(ErrorCode::limit_exceeded,"organic material allocation failed");}
      catch(const std::exception& e) {return fail<OrganicMaterial>(ErrorCode::backend_failure,e.what());}
}
Result<OrganicState> OrganicMaterial::makeState(OrganicStateData state) const {
    if(!definition_)return fail<OrganicState>(ErrorCode::invalid_input,"organic material is empty or moved from");
    try {
        if(auto status=validateState(*definition_,state);!status)return std::unexpected(status.error());
        OrganicState result;result.owner_=definition_;result.data_=std::move(state);return result;
    } catch(const std::bad_alloc&) {return fail<OrganicState>(ErrorCode::limit_exceeded,"organic state allocation failed");}
      catch(const std::exception& e) {return fail<OrganicState>(ErrorCode::backend_failure,e.what());}
}
Result<OrganicState> OrganicMaterial::advance(const OrganicState& state,const OrganicStateModel& model,double seconds) const {
    if(!definition_||state.owner_.get()!=definition_.get())return fail<OrganicState>(ErrorCode::invalid_input,"state belongs to another organic material");
    if(!std::isfinite(seconds)||seconds<=0)return fail<OrganicState>(ErrorCode::invalid_input,"time step must be positive and finite");
    const double next=state.data_.timeSeconds+seconds;
    if(!std::isfinite(next)||next<=state.data_.timeSeconds)return fail<OrganicState>(ErrorCode::invalid_input,"next state time is not representable");
    try {
        auto draft=state.data_;
        if(auto status=model.step(*definition_,draft,seconds);!status)return std::unexpected(status.error());
        draft.timeSeconds=next;
        return makeState(std::move(draft));
    } catch(const std::bad_alloc&) {return fail<OrganicState>(ErrorCode::limit_exceeded,"organic model allocation failed");}
      catch(const std::exception& e) {return fail<OrganicState>(ErrorCode::backend_failure,e.what());}
      catch(...) {return fail<OrganicState>(ErrorCode::backend_failure,"organic model failed");}
}
Result<ResolvedMaterial> OrganicMaterial::resolve(const OrganicState& state) const {
    if(!definition_||state.owner_.get()!=definition_.get())return fail<ResolvedMaterial>(ErrorCode::invalid_input,"state belongs to another organic material");
    try {
        MaterialCatalog catalog;if(auto status=catalog.add(physicalForState(*definition_,state.data_),definition_->customAtomTypes);!status)return std::unexpected(status.error());
        auto resolved=catalog.resolveId(definition_->physical.id);if(!resolved)return resolved;
        resolved->surface=definition_->surface;return resolved;
    } catch(const std::bad_alloc&) {return fail<ResolvedMaterial>(ErrorCode::limit_exceeded,"organic material resolution allocation failed");}
      catch(const std::exception& e) {return fail<ResolvedMaterial>(ErrorCode::backend_failure,e.what());}
}
}
