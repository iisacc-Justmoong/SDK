#include "CustomAtom.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <set>
#include <type_traits>
#include <utility>

namespace ii::pbr {
bool isValidCustomIdentifier(std::string_view id) noexcept {
    const auto alphaNum=[](unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9');};
    if(id.empty()||id.size()>128||!alphaNum(static_cast<unsigned char>(id.front())))return false;
    return std::all_of(id.begin(),id.end(),[&](unsigned char c){return alphaNum(c)||c=='_'||c=='-'||c=='.'||c==':'||c=='/';});
}
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string message) {return std::unexpected(Error{code,std::move(message)});}
bool validSymbol(const std::string& text) {
    if(text.empty()||text.size()>32)return false;
    for(std::size_t i=0;i<text.size();) {
        const auto b=static_cast<unsigned char>(text[i]);std::uint32_t cp=0;std::size_t n=0;
        if(b<0x80){cp=b;n=1;}else if(b>=0xc2&&b<=0xdf){cp=b&0x1f;n=2;}
        else if(b>=0xe0&&b<=0xef){cp=b&0x0f;n=3;}else if(b>=0xf0&&b<=0xf4){cp=b&7;n=4;}else return false;
        if(i+n>text.size())return false;
        for(std::size_t j=1;j<n;++j){auto c=static_cast<unsigned char>(text[i+j]);if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&0x3f);}
        if((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||cp<0x20||
           (cp>=0x7f&&cp<=0x9f)||(cp>=0xd800&&cp<=0xdfff)||cp>0x10ffff||cp==0xfffe||cp==0xffff)return false;
        i+=n;
    }
    return true;
}
bool validText(const std::string& s){return s.size()<=4*1024*1024;}
bool positive(const std::optional<double>& value){return !value||(std::isfinite(*value)&&*value>0);}
}
Result<CustomAtomType> CustomAtomType::create(CustomAtomDefinition d) {
    if(!isValidCustomIdentifier(d.id)||!validSymbol(d.symbol)||d.name.empty()||!validText(d.name)||
       (d.molarMassGramsPerMol&&(!std::isfinite(*d.molarMassGramsPerMol)||*d.molarMassGramsPerMol<0))||
       !positive(d.covalentRadiusMetres)||!positive(d.vanDerWaalsRadiusMetres))
        return fail<CustomAtomType>(ErrorCode::invalid_input,"invalid custom atom identity, symbol or physical value");
    if(d.properties.size()>4096)return fail<CustomAtomType>(ErrorCode::limit_exceeded,"too many custom atom properties");
    try {
        std::set<std::string,std::less<>> keys;
        for(const auto& property:d.properties) {
            if(property.key.empty()||!validText(property.key)||!validText(property.unit)||!validText(property.source)||!keys.insert(property.key).second)
                return fail<CustomAtomType>(ErrorCode::invalid_input,"invalid or duplicate custom atom property");
            const auto valid=std::visit([](const auto& value){
                using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,double>)return std::isfinite(value);
                else if constexpr(std::is_same_v<T,std::string>)return validText(value);
                else if constexpr(std::is_same_v<T,std::vector<double>>)return value.size()<=1000000&&std::all_of(value.begin(),value.end(),[](double x){return std::isfinite(x);});
                else if constexpr(std::is_same_v<T,std::vector<std::int64_t>>)return value.size()<=1000000;
                else if constexpr(std::is_same_v<T,std::vector<std::string>>)return value.size()<=1000000&&std::all_of(value.begin(),value.end(),[](const auto& x){return validText(x);});
                else return true;
            },property.value);
            if(!valid)return fail<CustomAtomType>(ErrorCode::invalid_input,"invalid custom atom property value");
        }
        std::sort(d.properties.begin(),d.properties.end(),[](const auto& a,const auto& b){return a.key<b.key;});
        return CustomAtomType(std::make_shared<const CustomAtomDefinition>(std::move(d)));
    } catch(const std::bad_alloc&) {return fail<CustomAtomType>(ErrorCode::limit_exceeded,"custom atom allocation failed");}
}
}
