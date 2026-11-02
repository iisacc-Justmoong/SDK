#include "CustomBond.h"
#include "MolecularMap.h"
#include "OrganicMaterial.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace ii::pbr;
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x "\n";++failures;}} while(false)
bool relative(double a,double b) {return std::abs(a-b)<=std::max(std::abs(a),std::abs(b))*1e-10;}
CustomAtomType atom(std::string id,std::string symbol) {
    CustomAtomDefinition d;d.id=std::move(id);d.symbol=std::move(symbol);d.name="fixture";d.molarMassGramsPerMol=10;
    return *CustomAtomType::create(std::move(d));
}
CustomBondDefinition definition() {
    CustomBondDefinition d;d.id="test:binding";d.name="Example bond";d.kind="custom:coordinate";
    d.direction=BondDirection::firstToSecond;d.display=CustomBondDisplay::dashed;
    d.first={BondEndpointKind::customAtom,"test:A"};d.second={BondEndpointKind::customAtom,"test:B"};
    d.order=1.5;d.dissociationEnergyJoulesPerMol=250000;d.energySource="synthetic fixture";
    d.equilibriumLengthMetres=1.5e-10;d.stiffnessNewtonsPerMetre=100;d.breakDistanceMetres=3e-10;
    d.energyTemperatureKelvin=300;d.properties.push_back({"coupling",0.25,"1","fixture"});return d;
}
struct UserPotential final:BondPotential {
    Result<BondPotentialSample> evaluate(const CustomBondDefinition&,double) const override {return BondPotentialSample{-2.0,3.0};}
};
struct InvalidPotential final:BondPotential {
    Result<BondPotentialSample> evaluate(const CustomBondDefinition&,double) const override {return BondPotentialSample{std::numeric_limits<double>::quiet_NaN(),0};}
};
struct ThrowPotential final:BondPotential {
    Result<BondPotentialSample> evaluate(const CustomBondDefinition&,double) const override {throw std::runtime_error("model error");}
};
struct Sink final:MolecularMapSink {
    int custom=0,arrows=0;
    Result<void> stroke(const BondStroke& s) override {if(s.customBondId=="test:binding")++custom;if(s.arrowHead)++arrows;return {};}
    Result<void> wedge(const BondWedge&) override {return {};}
    Result<void> atom(const AtomGlyph&) override {return {};}
};
void definitionsAndEnergy() {
    auto d=definition();auto type=CustomBondType::create(d);CHECK(type);if(!type)return;
    CHECK(type->definition().dissociationEnergyJoulesPerMol==250000&&type->definition().order==1.5);
    d.name="changed";CHECK(type->definition().name=="Example bond");
    auto bad=definition();bad.id="bad}id";CHECK(!CustomBondType::create(bad));
    bad=definition();bad.dissociationEnergyJoulesPerMol=-1;CHECK(!CustomBondType::create(bad));
    bad=definition();bad.energySource.clear();CHECK(!CustomBondType::create(bad));
    bad=definition();bad.stiffnessNewtonsPerMetre=-1;CHECK(!CustomBondType::create(bad));
    bad=definition();bad.equilibriumLengthMetres=0;CHECK(!CustomBondType::create(bad));
    bad=definition();bad.order=std::numeric_limits<double>::quiet_NaN();CHECK(!CustomBondType::create(bad));
    bad=definition();bad.first={BondEndpointKind::element,"Xx"};CHECK(!CustomBondType::create(bad));
    bad=definition();bad.dissociationEnergyJoulesPerMol.reset();CHECK(!CustomBondType::create(bad));
    bad=definition();bad.direction=static_cast<BondDirection>(99);CHECK(!CustomBondType::create(bad));
    bad=definition();bad.properties.push_back(bad.properties[0]);CHECK(!CustomBondType::create(bad));
    HarmonicBondPotential harmonic;
    auto sample=evaluateBondPotential(*type,1.6e-10,harmonic);CHECK(sample);
    if(sample){CHECK(relative(sample->energyJoules,5e-21));CHECK(relative(sample->radialForceNewtons,-1e-9));}
    auto left=evaluateBondPotential(*type,1.599e-10,harmonic),right=evaluateBondPotential(*type,1.601e-10,harmonic);
    if(sample&&left&&right)CHECK(std::abs((left->energyJoules-right->energyJoules)/(2e-13)-sample->radialForceNewtons)<1e-18);
    auto compressed=evaluateBondPotential(*type,1.4e-10,harmonic);CHECK(compressed&&compressed->radialForceNewtons>0);
    CHECK(!evaluateBondPotential(*type,-1,harmonic));
    CHECK(!evaluateBondPotential(*type,std::numeric_limits<double>::infinity(),harmonic));
    CHECK(!evaluateBondPotential(*type,std::numeric_limits<double>::max(),harmonic));
    auto zero=definition();zero.stiffnessNewtonsPerMetre=0;auto zeroType=CustomBondType::create(zero);CHECK(zeroType);
    if(zeroType){auto result=evaluateBondPotential(*zeroType,std::numeric_limits<double>::max(),harmonic);CHECK(result&&result->energyJoules==0&&result->radialForceNewtons==0);}
    auto tiny=definition();tiny.equilibriumLengthMetres=1;tiny.stiffnessNewtonsPerMetre=std::numeric_limits<double>::denorm_min();
    auto tinyType=CustomBondType::create(tiny);CHECK(tinyType);if(tinyType)CHECK(!evaluateBondPotential(*tinyType,2,harmonic));
    auto missing=definition();missing.stiffnessNewtonsPerMetre.reset();auto incomplete=CustomBondType::create(missing);CHECK(incomplete);
    if(incomplete)CHECK(!evaluateBondPotential(*incomplete,1e-10,harmonic));
    UserPotential user;auto external=evaluateBondPotential(*type,1e-10,user);CHECK(external&&external->energyJoules==-2&&external->radialForceNewtons==3);
    InvalidPotential invalid;ThrowPotential throwing;CHECK(!evaluateBondPotential(*type,1e-10,invalid));CHECK(!evaluateBondPotential(*type,1e-10,throwing));
}
void moleculeAndMap() {
    auto a=atom("test:A","A*"),b=atom("test:B","B*");auto type=CustomBondType::create(definition());CHECK(type);if(!type)return;
    auto edge=Bond::fromCustom(0,1,*type);auto molecule=Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{edge});CHECK(molecule);if(!molecule)return;
    CHECK(molecule->bonds()[0].order==BondOrder::custom&&molecule->bonds()[0].custom&&molecule->customBondTypes().size()==1);
    CHECK(!Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{Bond::fromCustom(1,0,*type)}));
    auto malformed=edge;malformed.order=BondOrder::single;CHECK(!Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{malformed}));
    Bond missingType;missingType.first=0;missingType.second=1;missingType.order=BondOrder::custom;
    CHECK(!Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{missingType}));
    CHECK(!Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{edge,{0,1,BondOrder::single}}));
    auto graph=buildMolecularMap(*molecule);CHECK(graph);if(graph) {
        CHECK(graph->strokes().size()==3&&graph->strokes()[0].dashed&&graph->strokes()[0].customBondId=="test:binding");
        Sink sink;CHECK(drawMolecularMap(*graph,sink));CHECK(sink.custom==3&&sink.arrows==2);
        auto svg=molecularMapSvg(*graph);CHECK(svg&&svg->find("data-custom-bond=\"test:binding\"")!=std::string::npos&&svg->find("data-bond-arrow=\"true\"")!=std::string::npos);
    }
    auto wedged=Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{Bond::fromCustom(0,1,*type,BondStereo::hashedWedge)});CHECK(wedged);
    if(wedged){auto diagram=buildMolecularMap(*wedged);CHECK(diagram&&diagram->wedges().size()==1&&diagram->wedges()[0].customBondId=="test:binding"&&diagram->strokes().size()==2);}
    auto owned=[&]{auto temporary=CustomBondType::create(definition());return Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{Bond::fromCustom(0,1,*temporary)});}();
    CHECK(owned&&owned->bonds()[0].custom&&owned->bonds()[0].custom->definition().id=="test:binding");
    auto symmetric=definition();symmetric.id="test:symmetric";symmetric.direction=BondDirection::undirected;
    auto symmetricType=CustomBondType::create(symmetric);CHECK(symmetricType);
    if(symmetricType)CHECK(Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{Bond::fromCustom(1,0,*symmetricType)}));
    auto mixed=definition();mixed.id="test:mixed";mixed.second={BondEndpointKind::element,"O"};auto mixedType=CustomBondType::create(mixed);CHECK(mixedType);
    if(mixedType){CHECK(Molecule::create({Atom::fromCustom(a),{"O"}},{Bond::fromCustom(0,1,*mixedType)}));
        CHECK(!Molecule::create({Atom::fromCustom(a),{"O"}},{Bond::fromCustom(1,0,*mixedType)}));}
    auto conflict=definition();conflict.dissociationEnergyJoulesPerMol=100;auto conflictType=CustomBondType::create(conflict);CHECK(conflictType);
    if(conflictType){CHECK(!Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b),Atom::fromCustom(b)},{edge,Bond::fromCustom(0,2,*conflictType)}));
        auto alternate=Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{Bond::fromCustom(0,1,*conflictType)});CHECK(alternate);
        if(alternate){std::array alternatives{*alternate};auto family=molecule->withIsomers(alternatives);CHECK(family);
            if(family){auto chosen=family->isomer(0);CHECK(chosen&&chosen->bonds()[0].custom&&chosen->bonds()[0].custom->definition().dissociationEnergyJoulesPerMol==100);}}}
    OrganicMaterialDefinition organic;organic.physical={"test:custom-bonds","Custom","","solid",1000,{},{},{},{},{},"fixture"};
    OrganicComponent component;component.id="molecule";component.name="Molecule";component.kind="molecule";component.molecule=*molecule;organic.components.push_back(component);
    auto material=OrganicMaterial::create(organic);CHECK(material&&material->definition().components[0].molecule->bonds()[0].custom);
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--emit-custom-bond-svg") {
        auto a=atom("test:A","A*"),b=atom("test:B","B*");auto type=CustomBondType::create(definition());if(!type)return 2;
        auto mol=Molecule::create({Atom::fromCustom(a),Atom::fromCustom(b)},{Bond::fromCustom(0,1,*type)});if(!mol)return 3;
        auto map=buildMolecularMap(*mol);if(!map)return 4;auto svg=molecularMapSvg(*map);if(!svg)return 5;std::cout<<*svg;return 0;
    }
    definitionsAndEnergy();moleculeAndMap();return failures?1:0;
}
