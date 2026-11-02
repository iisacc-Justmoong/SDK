#include "CustomAtom.h"
#include "MolecularMap.h"
#include "OrganicMaterial.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace ii::pbr;
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x "\n";++failures;}} while(false)
bool near(double a,double b){return std::abs(a-b)<1e-8;}
CustomAtomDefinition definition() {
    CustomAtomDefinition d;d.id="iisacc:example";d.symbol="X&";d.name="Example atom";
    d.protons=140;d.neutrons=180;d.electrons=139;d.molarMassGramsPerMol=42.5;d.covalentRadiusMetres=1.2e-10;
    d.properties.push_back({"spin",0.5,"hbar","test fixture"});return d;
}
void constructionAndFormula() {
    auto d=definition();auto type=CustomAtomType::create(d);CHECK(type);if(!type)return;
    CHECK(type->definition().protons==140&&type->definition().properties.size()==1);
    auto invalid=d;invalid.id="bad}token";CHECK(!CustomAtomType::create(invalid));
    invalid=d;invalid.symbol=std::string("X\x01",2);CHECK(!CustomAtomType::create(invalid));
    invalid=d;invalid.symbol=std::string("\xc0\xaf",2);CHECK(!CustomAtomType::create(invalid));
    invalid=d;invalid.molarMassGramsPerMol=std::numeric_limits<double>::quiet_NaN();CHECK(!CustomAtomType::create(invalid));
    invalid=d;invalid.covalentRadiusMetres=0;CHECK(!CustomAtomType::create(invalid));
    invalid=d;invalid.properties.push_back(invalid.properties[0]);CHECK(!CustomAtomType::create(invalid));
    std::array types{*type};
    CHECK(!parseMolecularFormula("H{iisacc:example}2"));
    auto formula=parseMolecularFormula("{iisacc:example}2H",types);CHECK(formula);
    if(formula){CHECK(formula->canonical=="H{iisacc:example}2");CHECK(formula->atoms.at("{iisacc:example}")==2);CHECK(formula->molarMassGramsPerMol&&near(*formula->molarMassGramsPerMol,86.008));}
    auto nested=parseMolecularFormula("(C{iisacc:example})2.H2O",types);
    CHECK(nested&&nested->canonical=="C2H2O{iisacc:example}2");
    CHECK(!parseMolecularFormula("{unknown}",types));CHECK(!parseMolecularFormula("{iisacc:example",types));
    auto unicode=d;unicode.symbol="원자";CHECK(CustomAtomType::create(unicode));
    auto localCopy=d;auto stable=CustomAtomType::create(localCopy);localCopy.symbol="changed";
    CHECK(stable&&stable->definition().symbol=="X&");
    auto unordered=d;unordered.properties.push_back({"charge",std::int64_t(1),"e","fixture"});
    auto equivalent=unordered;std::reverse(equivalent.properties.begin(),equivalent.properties.end());
    auto orderedType=CustomAtomType::create(unordered),equivalentType=CustomAtomType::create(equivalent);
    CHECK(orderedType&&equivalentType&&orderedType->sameDefinition(*equivalentType));
    invalid=d;invalid.molarMassGramsPerMol.reset();auto noMass=CustomAtomType::create(invalid);CHECK(noMass);
    if(noMass){std::array unknown{*noMass};auto incomplete=parseMolecularFormula("{iisacc:example}",unknown);CHECK(incomplete&&!incomplete->molarMassGramsPerMol);}
    auto changed=d;changed.molarMassGramsPerMol=43;auto collision=CustomAtomType::create(changed);CHECK(collision);
    if(collision){std::array conflict{*type,*collision};CHECK(!parseMolecularFormula("{iisacc:example}",conflict));}
    std::vector<Atom> longAtoms;
    for(int i=0;i<12;++i){auto longDefinition=d;longDefinition.id="x"+std::string(100,'a')+std::to_string(i);
        auto longType=CustomAtomType::create(std::move(longDefinition));CHECK(longType);if(longType)longAtoms.push_back(Atom::fromCustom(*longType));}
    auto large=Molecule::create(std::move(longAtoms),{});CHECK(large&&large->formula().canonical.size()>1024&&large->customAtomTypes().size()==12);
}
void moleculeAndMaterials() {
    auto type=CustomAtomType::create(definition());CHECK(type);if(!type)return;
    auto custom=Atom::fromCustom(*type,0,0,1);
    auto molecule=Molecule::create({custom,{"O",0,0,1}},{{0,1}},"H2O{iisacc:example}");CHECK(molecule);if(!molecule)return;
    CHECK(molecule->formula().canonical=="H2O{iisacc:example}"&&molecule->customAtomTypes().size()==1);
    CHECK(molecule->formula().molarMassGramsPerMol&&near(*molecule->formula().molarMassGramsPerMol,60.515));
    CHECK(molecule->matchesFormula("{iisacc:example}OH2")&&*molecule->matchesFormula("{iisacc:example}OH2"));
    auto map=buildMolecularMap(*molecule);CHECK(map);if(map) {
        CHECK(map->atoms()[0].label=="X&H"&&map->atoms()[0].customAtomId=="iisacc:example");
        auto svg=molecularMapSvg(*map);CHECK(svg&&svg->find("X&amp;H")!=std::string::npos&&svg->find("data-custom-atom=\"iisacc:example\"")!=std::string::npos);
    }
    auto other=Molecule::create({{"O",0,0,1},custom},{{0,1}},"H2O{iisacc:example}");CHECK(other);if(!other)return;
    std::array options{*other};auto family=molecule->withIsomers(options);CHECK(family);
    if(family){auto selected=family->isomer(0);CHECK(selected&&selected->atoms()[1].custom&&selected->atoms()[1].custom->definition().id=="iisacc:example");CHECK(buildIsomerMap(*family,0));}
    auto isotope=custom;isotope.isotopeMassNumber=3;auto labelled=Molecule::create({isotope},{});CHECK(labelled&&!labelled->formula().molarMassGramsPerMol);
    auto malformed=custom;malformed.element="C";CHECK(!Molecule::create({malformed},{}));
    auto changed=definition();changed.molarMassGramsPerMol=43;auto conflicting=CustomAtomType::create(changed);CHECK(conflicting);
    if(conflicting){auto altered=Molecule::create({Atom::fromCustom(*conflicting,0,0,1),{"O",0,0,1}},{{0,1}});CHECK(altered);
        if(altered){std::array alternatives{*altered};CHECK(!molecule->withIsomers(alternatives));}
        CHECK(!Molecule::create({custom,Atom::fromCustom(*conflicting)},{}));}
    auto ordinary=Molecule::create({{"C"}},{});auto fake=definition();fake.id="carbon-model";fake.symbol="C";
    auto fakeType=CustomAtomType::create(fake);CHECK(ordinary&&fakeType);
    if(ordinary&&fakeType){auto fakeCarbon=Molecule::create({Atom::fromCustom(*fakeType)},{});CHECK(fakeCarbon);if(fakeCarbon){std::array alt{*fakeCarbon};CHECK(!ordinary->withIsomers(alt));}}
    MaterialCatalog catalog;MaterialRecord record{"test:custom","Synthetic material","H2O{iisacc:example}","solid",1100,{},{},{},{},{},"fixture"};
    std::array types{*type};CHECK(!catalog.add(record));CHECK(catalog.add(record,types));
    auto resolved=catalog.resolve(*molecule,"test:custom");CHECK(resolved&&resolved->molecular&&resolved->molecular->molarMassGramsPerMol&&near(*resolved->molecular->molarMassGramsPerMol,60.515));
    auto byText=catalog.resolve("H2O{iisacc:example}",types,"test:custom");CHECK(byText);
    if(family)CHECK(catalog.resolveIsomer(*family,0,"test:custom"));
    if(conflicting){std::array mismatched{*conflicting};CHECK(!catalog.resolve("H2O{iisacc:example}",mismatched,"test:custom"));}
    if(conflicting){auto secondRecord=record;secondRecord.id="test:conflict";std::array mismatched{*conflicting};CHECK(!catalog.add(secondRecord,mismatched));}
    auto found=catalog.candidates("H2O{iisacc:example}",types);CHECK(found&&found->size()==1);
    OrganicMaterialDefinition d;d.physical=record;d.customAtomTypes.assign(types.begin(),types.end());
    OrganicComponent component;component.id="molecule";component.name="Custom molecule";component.kind="molecule";
    component.molecularFormula="H2O{iisacc:example}";component.molecule=*molecule;d.components.push_back(component);
    auto organic=OrganicMaterial::create(d);CHECK(organic);if(organic){auto state=organic->makeState();CHECK(state&&organic->resolve(*state));}
    d.customAtomTypes.clear();CHECK(!OrganicMaterial::create(d));
    d.physical.formula.clear();CHECK(OrganicMaterial::create(d)); // molecule carries its own type
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--emit-custom-svg") {
        auto type=CustomAtomType::create(definition());if(!type)return 2;
        auto molecule=Molecule::create({Atom::fromCustom(*type,0,0,1),{"O",0,0,1}},{{0,1}});if(!molecule)return 3;
        auto map=buildMolecularMap(*molecule);if(!map)return 4;
        auto svg=molecularMapSvg(*map);if(!svg)return 5;std::cout<<*svg;return 0;
    }
    constructionAndFormula();moleculeAndMaterials();return failures?1:0;
}
