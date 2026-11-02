#include "CustomBond.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <set>
#include <type_traits>
#include <utility>

namespace ii::pbr {
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string message) {return std::unexpected(Error{code,std::move(message)});}
bool validText(const std::string& s){return s.size()<=4*1024*1024;}
bool positive(const std::optional<double>& v){return !v||(std::isfinite(*v)&&*v>0);}
bool nonnegative(const std::optional<double>& v){return !v||(std::isfinite(*v)&&*v>=0);}
bool endpoint(const BondEndpoint& e) {
    switch(e.kind){
    case BondEndpointKind::any:return e.id.empty();
    case BondEndpointKind::element:return isKnownElementSymbol(e.id);
    case BondEndpointKind::customAtom:return isValidCustomIdentifier(e.id);
    }
    return false;
}
}
Result<CustomBondType> CustomBondType::create(CustomBondDefinition d) {
    if(!isValidCustomIdentifier(d.id)||d.name.empty()||!validText(d.name)||d.kind.empty()||!validText(d.kind)||
       !endpoint(d.first)||!endpoint(d.second)||!validText(d.energySource)||
       (d.direction!=BondDirection::undirected&&d.direction!=BondDirection::firstToSecond)||
       (d.display!=CustomBondDisplay::solid&&d.display!=CustomBondDisplay::dashed&&
        d.display!=CustomBondDisplay::doubleLine&&d.display!=CustomBondDisplay::tripleLine)||
       !nonnegative(d.order)||!nonnegative(d.dissociationEnergyJoulesPerMol)||!positive(d.equilibriumLengthMetres)||
       !nonnegative(d.stiffnessNewtonsPerMetre)||!positive(d.breakDistanceMetres)||
       !positive(d.energyTemperatureKelvin)||!positive(d.energyPressurePascal)||
       (d.dissociationEnergyJoulesPerMol&&d.energySource.empty())||
       (!d.dissociationEnergyJoulesPerMol&&(!d.energySource.empty()||d.energyTemperatureKelvin||d.energyPressurePascal)))
        return fail<CustomBondType>(ErrorCode::invalid_input,"invalid custom bond definition or energy provenance");
    if(d.properties.size()>4096)return fail<CustomBondType>(ErrorCode::limit_exceeded,"too many custom bond properties");
    try {
        std::set<std::string,std::less<>> keys;
        for(const auto& property:d.properties) {
            if(property.key.empty()||!validText(property.key)||!validText(property.unit)||!validText(property.source)||!keys.insert(property.key).second)
                return fail<CustomBondType>(ErrorCode::invalid_input,"invalid or duplicate custom bond property");
            const auto valid=std::visit([](const auto& value){
                using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,double>)return std::isfinite(value);
                else if constexpr(std::is_same_v<T,std::string>)return validText(value);
                else if constexpr(std::is_same_v<T,std::vector<double>>)return value.size()<=1000000&&std::all_of(value.begin(),value.end(),[](double x){return std::isfinite(x);});
                else if constexpr(std::is_same_v<T,std::vector<std::int64_t>>)return value.size()<=1000000;
                else if constexpr(std::is_same_v<T,std::vector<std::string>>)return value.size()<=1000000&&std::all_of(value.begin(),value.end(),[](const auto& x){return validText(x);});
                else return true;
            },property.value);
            if(!valid)return fail<CustomBondType>(ErrorCode::invalid_input,"invalid custom bond property value");
        }
        std::sort(d.properties.begin(),d.properties.end(),[](const auto& a,const auto& b){return a.key<b.key;});
        return CustomBondType(std::make_shared<const CustomBondDefinition>(std::move(d)));
    } catch(const std::bad_alloc&) {return fail<CustomBondType>(ErrorCode::limit_exceeded,"custom bond allocation failed");}
}
Result<BondPotentialSample> HarmonicBondPotential::evaluate(const CustomBondDefinition& d,double distance) const {
    if(!std::isfinite(distance)||distance<0)return fail<BondPotentialSample>(ErrorCode::invalid_input,"invalid bond distance");
    if(!d.equilibriumLengthMetres||!d.stiffnessNewtonsPerMetre)return fail<BondPotentialSample>(ErrorCode::unsupported,"harmonic bond requires equilibrium length and stiffness");
    if(!std::isfinite(*d.equilibriumLengthMetres)||*d.equilibriumLengthMetres<=0||!std::isfinite(*d.stiffnessNewtonsPerMetre)||*d.stiffnessNewtonsPerMetre<0)
        return fail<BondPotentialSample>(ErrorCode::invalid_input,"invalid harmonic parameters");
    if(*d.stiffnessNewtonsPerMetre==0)return BondPotentialSample{};
    const long double delta=static_cast<long double>(distance)-*d.equilibriumLengthMetres;
    const long double force=-static_cast<long double>(*d.stiffnessNewtonsPerMetre)*delta;
    const long double energy=-0.5L*force*delta;
    const auto max=std::numeric_limits<double>::max();
    if(!std::isfinite(force)||!std::isfinite(energy)||std::abs(force)>max||energy>max||
       (delta!=0&&force==0)||(delta!=0&&force!=0&&energy==0)||
       (force!=0&&double(force)==0)||(energy!=0&&double(energy)==0))
        return fail<BondPotentialSample>(ErrorCode::limit_exceeded,"bond potential is not representable");
    return BondPotentialSample{double(energy),double(force)};
}
Result<BondPotentialSample> evaluateBondPotential(const CustomBondType& type,double distance,const BondPotential& model) {
    if(!type.valid()||!std::isfinite(distance)||distance<0)return fail<BondPotentialSample>(ErrorCode::invalid_input,"invalid custom bond type or distance");
    try {
        auto value=model.evaluate(type.definition(),distance);if(!value)return value;
        if(!std::isfinite(value->energyJoules)||!std::isfinite(value->radialForceNewtons))
            return fail<BondPotentialSample>(ErrorCode::backend_failure,"bond potential returned non-finite values");
        return value;
    } catch(const std::bad_alloc&) {return fail<BondPotentialSample>(ErrorCode::limit_exceeded,"bond potential allocation failed");}
      catch(const std::exception& e) {return fail<BondPotentialSample>(ErrorCode::backend_failure,e.what());}
      catch(...) {return fail<BondPotentialSample>(ErrorCode::backend_failure,"bond potential failed");}
}
}
