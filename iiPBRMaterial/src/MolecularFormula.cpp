#include "MolecularFormula.h"
#include "CustomAtom.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace ii::pbr {
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string message) { return std::unexpected(Error{code,std::move(message)}); }
constexpr std::uint64_t atomLimit=1000000000ULL;
constexpr std::string_view symbols=" H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn Ga Ge As Se Br Kr Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce Pr Nd Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn Fr Ra Ac Th Pa U Np Pu Am Cm Bk Cf Es Fm Md No Lr Rf Db Sg Bh Hs Mt Ds Rg Cn Nh Fl Mc Lv Ts Og ";
bool element(std::string_view s) noexcept {
    if (s.empty() || s.size()>2) return false;
    for (std::size_t pos=1;pos<symbols.size();) {
        const auto end=symbols.find(' ',pos);
        if (symbols.substr(pos,end-pos)==s) return true;
        if (end==std::string_view::npos) break;
        pos=end+1;
    }
    return false;
}
std::optional<double> atomicWeight(std::string_view s) {
    // Conventional natural-isotope weights, g/mol. Unknown weights remain absent.
    static const std::map<std::string_view,double> weights{
        {"H",1.008},{"He",4.002602},{"Li",6.94},{"B",10.81},{"C",12.011},{"N",14.007},
        {"O",15.999},{"F",18.998403163},{"Na",22.98976928},{"Mg",24.305},{"Al",26.9815385},
        {"Si",28.085},{"P",30.973761998},{"S",32.06},{"Cl",35.45},{"K",39.0983},
        {"Ca",40.078},{"Ti",47.867},{"Cr",51.9961},{"Mn",54.938044},{"Fe",55.845},
        {"Co",58.933194},{"Ni",58.6934},{"Cu",63.546},{"Zn",65.38},{"Br",79.904},
        {"Ag",107.8682},{"I",126.90447},{"Au",196.966569},{"Hg",200.592},{"Pb",207.2}
    };
    const auto i=weights.find(s); return i==weights.end()?std::nullopt:std::optional(i->second);
}
using Counts=std::map<std::string,std::uint64_t>;
using CustomTypes=std::map<std::string,const CustomAtomType*,std::less<>>;
class Parser {
public:
    Parser(std::string_view text,const CustomTypes& custom):text_(text),custom_(custom){}
    Result<Counts> parse() {
        Counts result; bool firstTerm=true;
        while (pos_<text_.size()) {
            std::uint64_t coefficient=1;
            if (digit()) {
                if(firstTerm) return fail<Counts>(ErrorCode::invalid_input,"leading coefficients and isotope notation are unsupported");
                auto n=number(); if(!n)return std::unexpected(n.error()); coefficient=*n;
            }
            auto term=sequence(0,'\0'); if(!term)return std::unexpected(term.error());
            if (auto r=merge(result,*term,coefficient);!r)return std::unexpected(r.error());
            firstTerm=false;
            if (pos_==text_.size()) break;
            if (text_[pos_]=='.') ++pos_;
            else if (text_.substr(pos_,2)=="\xC2\xB7") pos_+=2;
            else return fail<Counts>(ErrorCode::invalid_input,"invalid formula separator");
            if (pos_==text_.size()) return fail<Counts>(ErrorCode::invalid_input,"empty formula term");
        }
        return result;
    }
private:
    bool digit() const { return pos_<text_.size() && text_[pos_]>='0' && text_[pos_]<='9'; }
    Result<std::uint64_t> number() {
        if (!digit() || text_[pos_]=='0') return fail<std::uint64_t>(ErrorCode::invalid_input,"counts must be positive integers without leading zeroes");
        std::uint64_t n=0;
        while(digit()) {
            unsigned d=unsigned(text_[pos_++]-'0');
            if (n>(atomLimit-d)/10) return fail<std::uint64_t>(ErrorCode::limit_exceeded,"formula count is too large");
            n=n*10+d;
        }
        return n;
    }
    Result<void> merge(Counts& into,const Counts& other,std::uint64_t factor) {
        for (const auto& [symbol,count]:other) {
            if (count>atomLimit/factor || into[symbol]>atomLimit-count*factor)
                return fail<void>(ErrorCode::limit_exceeded,"formula atom count is too large");
            into[symbol]+=count*factor;
        }
        return {};
    }
    Result<Counts> sequence(unsigned depth,char closing) {
        if (depth>16) return fail<Counts>(ErrorCode::limit_exceeded,"formula nesting is too deep");
        Counts result; bool any=false;
        while (pos_<text_.size()) {
            const char c=text_[pos_];
            if (c==closing && closing!='\0') break;
            if (!closing && (c=='.'||text_.substr(pos_,2)=="\xC2\xB7")) break;
            Counts group;
            if (c=='(' || c=='[') {
                ++pos_; const char expected=c=='('?')':']';
                auto inner=sequence(depth+1,expected); if(!inner)return std::unexpected(inner.error());
                if (pos_==text_.size()||text_[pos_]!=expected) return fail<Counts>(ErrorCode::invalid_input,"unclosed or mismatched group");
                ++pos_; group=std::move(*inner);
            } else if (c=='{') {
                const auto start=++pos_;const auto end=text_.find('}',start);
                if(end==std::string_view::npos)return fail<Counts>(ErrorCode::invalid_input,"unclosed custom atom identifier");
                const auto id=text_.substr(start,end-start);auto found=custom_.find(id);
                if(found==custom_.end())return fail<Counts>(ErrorCode::invalid_input,"custom atom identifier is not defined");
                group.emplace("{"+std::string(id)+"}",1);pos_=end+1;
            } else if (c>='A' && c<='Z') {
                const auto start=pos_++;
                if (pos_<text_.size()&&text_[pos_]>='a'&&text_[pos_]<='z') ++pos_;
                std::string symbol(text_.substr(start,pos_-start));
                if (!element(symbol)) return fail<Counts>(ErrorCode::invalid_input,"unknown chemical element: "+symbol);
                group.emplace(std::move(symbol),1);
            } else return fail<Counts>(ErrorCode::invalid_input,"unsupported character or formula notation");
            std::uint64_t count=1;
            if (digit()) { auto n=number(); if(!n)return std::unexpected(n.error()); count=*n; }
            if (auto r=merge(result,group,count);!r)return std::unexpected(r.error());
            any=true;
        }
        if (!any) return fail<Counts>(ErrorCode::invalid_input,"empty formula group or term");
        return result;
    }
    std::string_view text_;const CustomTypes& custom_;std::size_t pos_{};
};
}
bool isKnownElementSymbol(std::string_view symbol) noexcept { return element(symbol); }
Result<MolecularFormula> parseMolecularFormula(std::string_view text) {
    return parseMolecularFormula(text,{});
}
Result<MolecularFormula> parseMolecularFormula(std::string_view text,std::span<const CustomAtomType> customTypes) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    if(text.empty()) return fail<MolecularFormula>(ErrorCode::invalid_input,"formula is empty");
    const auto maxLength=customTypes.empty()?std::size_t(1024):std::size_t(1024*1024);
    if(text.size()>maxLength) return fail<MolecularFormula>(ErrorCode::limit_exceeded,"formula is too long");
    if(customTypes.size()>4096)return fail<MolecularFormula>(ErrorCode::limit_exceeded,"too many custom atom definitions");
    try {
        CustomTypes custom;
        for(const auto& type:customTypes) {
            if(!type.valid())return fail<MolecularFormula>(ErrorCode::invalid_input,"empty custom atom definition");
            auto [it,inserted]=custom.emplace(type.definition().id,&type);
            if(!inserted&&!it->second->sameDefinition(type))return fail<MolecularFormula>(ErrorCode::invalid_input,"conflicting custom atom definitions");
        }
        std::string normalized;normalized.reserve(text.size());
        for(std::size_t i=0;i<text.size();) {
            if(i+2<text.size()&&static_cast<unsigned char>(text[i])==0xe2&&
               static_cast<unsigned char>(text[i+1])==0x82&&static_cast<unsigned char>(text[i+2])>=0x80&&
               static_cast<unsigned char>(text[i+2])<=0x89) {
                normalized.push_back(char('0'+static_cast<unsigned char>(text[i+2])-0x80));i+=3;
            } else normalized.push_back(text[i++]);
        }
        Parser parser(normalized,custom); auto atoms=parser.parse(); if(!atoms)return std::unexpected(atoms.error());
        MolecularFormula result; result.input=std::string(text); result.atoms=std::move(*atoms);
        auto append=[&](const auto& e) { result.canonical+=e.first; if(e.second!=1)result.canonical+=std::to_string(e.second); };
        if (result.atoms.contains("C")) {
            append(*result.atoms.find("C")); if(result.atoms.contains("H"))append(*result.atoms.find("H"));
            for(const auto& e:result.atoms)if(e.first!="C"&&e.first!="H")append(e);
        } else for(const auto& e:result.atoms)append(e);
        double weight=0; bool complete=true;
        for(const auto& [name,count]:result.atoms) {
            std::optional<double> w;
            if(name.front()=='{')w=custom.at(name.substr(1,name.size()-2))->definition().molarMassGramsPerMol;
            else w=atomicWeight(name);
            if(!w){complete=false;break;}
            weight+=*w*double(count);
            if(!std::isfinite(weight))return fail<MolecularFormula>(ErrorCode::limit_exceeded,"molar mass is not representable");
        }
        if(complete)result.molarMassGramsPerMol=weight;
        return result;
    } catch (const std::bad_alloc&) { return fail<MolecularFormula>(ErrorCode::limit_exceeded,"formula allocation failed"); }
}
}
