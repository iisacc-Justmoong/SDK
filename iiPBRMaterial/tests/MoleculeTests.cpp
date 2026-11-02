#include "MolecularMap.h"
#include "PBRMaterial.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ii::pbr;
namespace {
int failures=0;
#define CHECK(x) do { if(!(x)) {std::cerr<<__LINE__<<": " #x "\n";++failures;} } while(false)
bool near(double a,double b) {return std::abs(a-b)<1e-8;}
struct Sink final:MolecularMapSink {
    int lines=0,wedges=0,atoms=0;bool error=false,outOfOrder=false;
    Result<void> stroke(const BondStroke&) override {if(atoms)outOfOrder=true;++lines;if(error)return std::unexpected(Error{ErrorCode::backend_failure,"sink failure"});return {};}
    Result<void> wedge(const BondWedge&) override {if(atoms)outOfOrder=true;++wedges;return {};}
    Result<void> atom(const AtomGlyph&) override {++atoms;return {};}
};
void object() {
    CHECK(isKnownElementSymbol("Fe"));CHECK(!isKnownElementSymbol("FE"));CHECK(!isKnownElementSymbol("C2"));
    std::vector<Atom> waterAtoms{{"O"},{"H"},{"H"}};
    std::vector<Bond> waterBonds{{0,1,BondOrder::single},{0,2,BondOrder::single}};
    auto water=Molecule::create(waterAtoms,waterBonds,"H2O");CHECK(water);if(!water)return;
    CHECK(water->formula().canonical=="H2O"&&water->formula().atoms.at("H")==2);
    CHECK(!Molecule::create(waterAtoms,waterBonds,"H2O2"));
    CHECK(!Molecule::create({},{}));
    auto badAtoms=waterAtoms;badAtoms[0].element="Xx";CHECK(!Molecule::create(badAtoms,waterBonds));
    badAtoms=waterAtoms;badAtoms[1].implicitHydrogens=1;CHECK(!Molecule::create(badAtoms,waterBonds));
    badAtoms=waterAtoms;badAtoms[0].isotopeMassNumber=401;CHECK(!Molecule::create(badAtoms,waterBonds));
    badAtoms=waterAtoms;badAtoms[0].formalCharge=17;CHECK(!Molecule::create(badAtoms,waterBonds));
    auto badBonds=waterBonds;badBonds[0].second=0;CHECK(!Molecule::create(waterAtoms,badBonds));
    badBonds=waterBonds;badBonds.push_back({1,0,BondOrder::doubleBond});CHECK(!Molecule::create(waterAtoms,badBonds));
    badBonds=waterBonds;badBonds[0].order=static_cast<BondOrder>(99);CHECK(!Molecule::create(waterAtoms,badBonds));
    badBonds=waterBonds;badBonds[0].order=BondOrder::doubleBond;badBonds[0].stereo=BondStereo::solidWedge;CHECK(!Molecule::create(waterAtoms,badBonds));
    auto catalog=MaterialCatalog::referenceCatalog();CHECK(catalog);
    if(catalog){auto material=catalog->resolve(*water,"nist:H2O:liquid:20C");CHECK(material&&near(material->physical.densityKgPerM3,998.20675));}
    auto methane=Molecule::create({{"C",13,-1,4}}, {}, "CH4");CHECK(methane);
    if(methane){CHECK(!methane->formula().molarMassGramsPerMol&&methane->totalFormalCharge()==-1);auto map=buildMolecularMap(*methane);CHECK(map&&map->atoms()[0].label=="[13]CH4-");}
    auto carbon13=Molecule::create({{"C",13}}, {}, "C");CHECK(carbon13);
    if(catalog&&carbon13){auto resolved=catalog->resolve(*carbon13,"nist:C:graphite");CHECK(resolved&&resolved->molecular&&!resolved->molecular->molarMassGramsPerMol);}
    // The same formula does not supply or identify its connectivity.
    auto ethanol=Molecule::create({{"C",0,0,3},{"C",0,0,2},{"O",0,0,1}},{{0,1,BondOrder::single},{1,2,BondOrder::single}},"C2H6O");
    auto ether=Molecule::create({{"C",0,0,3},{"O"},{"C",0,0,3}},{{0,1,BondOrder::single},{1,2,BondOrder::single}},"C2H6O");
    CHECK(ethanol&&ether);if(ethanol&&ether){CHECK(ethanol->formula().canonical==ether->formula().canonical);CHECK(ethanol->atoms()[1].element!=ether->atoms()[1].element);}
}
void maps() {
    auto water=Molecule::create({{"O"},{"H"},{"H"}},{{0,1,BondOrder::single},{0,2,BondOrder::single}},"H2O");CHECK(water);if(!water)return;
    const std::array<MapPoint,3> positions{{{50,20},{10,80},{90,80}}};
    auto map=buildMolecularMap(*water,positions);CHECK(map);if(!map)return;
    CHECK(map->formula()=="H2O"&&map->atoms().size()==3&&map->strokes().size()==2);
    CHECK(map->atoms()[0].label=="O"&&near(map->atoms()[0].center.x,50));
    auto svg=molecularMapSvg(*map);CHECK(svg);if(svg) {
        CHECK(svg->find("<svg xmlns=\"http://www.w3.org/2000/svg\"")!=std::string::npos);
        CHECK(svg->find("<title>H2O molecular map</title>")!=std::string::npos);
        CHECK(svg->find("data-atom=\"0\"")!=std::string::npos&&svg->find("data-bond=\"1\"")!=std::string::npos);
    }
    Sink sink;CHECK(drawMolecularMap(*map,sink));CHECK(sink.lines==2&&sink.atoms==3&&!sink.outOfOrder);
    Sink failing;failing.error=true;CHECK(!drawMolecularMap(*map,failing)&&failing.atoms==0);
    auto invalid=positions;invalid[0].x=std::numeric_limits<double>::quiet_NaN();CHECK(!buildMolecularMap(*water,invalid));
    invalid=positions;invalid[1]=invalid[0];CHECK(!buildMolecularMap(*water,invalid));
    CHECK(!buildMolecularMap(*water,std::span(positions).first(2)));
    MolecularSvgStyle style;style.fontSize=100;CHECK(!molecularMapSvg(*map,style));
    MapOptions tight;tight.padding=0;auto tightMap=buildMolecularMap(*water,positions,tight);CHECK(tightMap);
    if(tightMap){MolecularSvgStyle thick;thick.strokeWidth=20;auto image=molecularMapSvg(*tightMap,thick);CHECK(image&&image->find("viewBox=\"-14.000 -4.000 128.000 108.000\"")!=std::string::npos);}
    auto autoMap=buildMolecularMap(*water),again=buildMolecularMap(*water);CHECK(autoMap&&again);
    if(autoMap&&again)for(std::size_t i=0;i<3;++i)CHECK(near(autoMap->atoms()[i].center.x,again->atoms()[i].center.x)&&near(autoMap->atoms()[i].center.y,again->atoms()[i].center.y));
    std::vector<Atom> carbon(6);for(auto& a:carbon){a.element="C";a.implicitHydrogens=1;}
    std::vector<Bond> ring;for(std::uint32_t i=0;i<6;++i)ring.push_back({i,(i+1)%6,BondOrder::aromatic});
    auto benzene=Molecule::create(carbon,ring,"C6H6");CHECK(benzene);
    if(benzene){auto m=buildMolecularMap(*benzene);CHECK(m);if(m){CHECK(m->strokes().size()==12);CHECK(std::count_if(m->strokes().begin(),m->strokes().end(),[](auto x){return x.dashed;})==6);}}
    auto nitrogen=Molecule::create({{"N"},{"N"}},{{0,1,BondOrder::triple}},"N2");CHECK(nitrogen);
    if(nitrogen){auto m=buildMolecularMap(*nitrogen);CHECK(m&&m->strokes().size()==3);}
    std::vector<Atom> chain(20);for(auto& a:chain)a.element="C";
    std::vector<Bond> chainBonds;for(std::uint32_t i=0;i<19;++i)chainBonds.push_back({i,i+1,BondOrder::single});
    auto moleculeChain=Molecule::create(chain,chainBonds);CHECK(moleculeChain);
    if(moleculeChain){auto drawing=buildMolecularMap(*moleculeChain);CHECK(drawing&&drawing->atoms().size()==20&&drawing->strokes().size()==19);}
    auto wedge=Molecule::create({{"C",0,0,3},{"O",0,0,1}},{{0,1,BondOrder::single,BondStereo::solidWedge}},"CH4O");CHECK(wedge);
    if(wedge){auto m=buildMolecularMap(*wedge);CHECK(m);if(m){CHECK(m->wedges().size()==1&&!m->wedges()[0].hashed);auto image=molecularMapSvg(*m);CHECK(image&&image->find("<polygon")!=std::string::npos);Sink target;CHECK(drawMolecularMap(*m,target));CHECK(target.wedges==1&&target.atoms==2&&!target.outOfOrder);}}
    auto hashed=Molecule::create({{"C",0,0,3},{"O",0,0,1}},{{0,1,BondOrder::single,BondStereo::hashedWedge}},"CH4O");CHECK(hashed);
    if(hashed){auto m=buildMolecularMap(*hashed);CHECK(m);if(m){CHECK(m->wedges().size()==1&&m->wedges()[0].hashed);auto image=molecularMapSvg(*m);CHECK(image&&image->find("data-bond=\"0\"")!=std::string::npos);}}
    std::vector<Atom> many(257);for(auto& atom:many)atom.element="H";
    auto large=Molecule::create(many,{});CHECK(large);
    if(large){CHECK(!buildMolecularMap(*large));std::vector<MapPoint> locations;for(std::size_t i=0;i<many.size();++i)locations.push_back({double(i)*40,0});CHECK(buildMolecularMap(*large,locations));}
}
void isomers() {
    std::vector<Atom> ethanolAtoms{{"C",0,0,3},{"C",0,0,2},{"O",0,0,1}};
    std::vector<Bond> ethanolBonds{{0,1,BondOrder::single},{1,2,BondOrder::single}};
    auto ethanol=Molecule::create(ethanolAtoms,ethanolBonds,"C2H6O");
    auto ether=Molecule::create({{"C",0,0,3},{"O"},{"C",0,0,3}},{{0,1,BondOrder::single},{1,2,BondOrder::single}},"C2H6O");
    auto water=Molecule::create({{"O"},{"H"},{"H"}},{{0,1,BondOrder::single},{0,2,BondOrder::single}},"H2O");
    CHECK(ethanol&&ether&&water);if(!ethanol||!ether||!water)return;
    std::array alternatives{*ether};
    auto initial=Molecule::create(ethanolAtoms,ethanolBonds,"C2H6O",alternatives);
    auto attached=ethanol->withIsomers(alternatives);
    CHECK(initial&&attached);if(!initial||!attached)return;
    CHECK(initial->isomers().size()==1&&attached->isomers().size()==1&&ethanol->isomers().empty());
    CHECK(attached->isomers()[0].atoms[1].element=="O");
    auto chosen=attached->isomer(0);CHECK(chosen);
    if(chosen){CHECK(chosen->formula().canonical=="C2H6O"&&chosen->atoms()[1].element=="O");CHECK(chosen->isomers().empty());}
    auto diagram=buildIsomerMap(*attached,0);CHECK(diagram);
    if(diagram)CHECK(diagram->atoms()[1].element=="O"&&diagram->strokes().size()==2);
    auto primaryDiagram=buildMolecularMap(*attached);CHECK(primaryDiagram&&primaryDiagram->atoms()[1].element=="C");
    const std::array<MapPoint,3> positions{{{0,0},{80,0},{160,0}}};
    auto positioned=buildIsomerMap(*attached,0,positions);CHECK(positioned);
    if(positioned){CHECK(near(positioned->atoms()[1].center.x,80));auto svg=molecularMapSvg(*positioned);CHECK(svg&&svg->find("C2H6O molecular map")!=std::string::npos);}
    CHECK(!buildIsomerMap(*attached,0,std::span(positions).first(2)));
    auto owned=[&]{std::array temporary{*ether};return ethanol->withIsomers(temporary);}();
    CHECK(owned&&owned->isomer(0)&&owned->isomer(0)->atoms()[1].element=="O");
    std::array nested{*attached};auto unnested=ether->withIsomers(nested);CHECK(unnested);
    if(unnested){auto x=unnested->isomer(0);CHECK(x&&x->isomers().empty()&&x->atoms()[1].element=="C");}
    CHECK(!attached->isomer(1)&&!buildIsomerMap(*attached,1));
    std::array invalid{*water};CHECK(!ethanol->withIsomers(invalid));
    CHECK(!Molecule::create(ethanolAtoms,ethanolBonds,"C2H6O",invalid));
    CHECK(!attached->withIsomers(invalid)&&attached->isomers().size()==1);
    auto implicitWater=Molecule::create({{"O",0,0,2}}, {}, "H2O");CHECK(implicitWater);
    if(implicitWater){std::array alternateWater{*implicitWater};auto waterSet=water->withIsomers(alternateWater);CHECK(waterSet&&waterSet->isomer(0)&&waterSet->isomer(0)->atoms().size()==1);}
    auto cleared=attached->withIsomers({});CHECK(cleared&&cleared->isomers().empty()&&attached->isomers().size()==1);
    // The requested contract is composition equality only; no graph-identity
    // or chemical isomerism inference is imposed.
    std::array repeated{*ethanol,*ether};auto repeatedSet=ethanol->withIsomers(repeated);CHECK(repeatedSet&&repeatedSet->isomers().size()==2);
    auto carbon=Molecule::create({{"C"}},{}),carbon13=Molecule::create({{"C",13,1}},{});
    CHECK(carbon&&carbon13);if(carbon&&carbon13){std::array marked{*carbon13};auto set=carbon->withIsomers(marked);CHECK(set);if(set){auto x=set->isomer(0);CHECK(x&&x->totalFormalCharge()==1&&!x->formula().molarMassGramsPerMol);auto reference=MaterialCatalog::referenceCatalog();CHECK(reference);if(reference){auto result=reference->resolveIsomer(*set,0,"nist:C:graphite");CHECK(result&&result->molecular&&!result->molecular->molarMassGramsPerMol);}}}
    std::vector<Atom> chiral{{"C"},{"H"},{"F"},{"Cl"},{"Br"}};
    std::vector<Bond> solidBonds{{0,1,BondOrder::single,BondStereo::solidWedge},{0,2},{0,3},{0,4}};
    auto hashedBonds=solidBonds;hashedBonds[0].stereo=BondStereo::hashedWedge;
    auto solid=Molecule::create(chiral,solidBonds),hashed=Molecule::create(chiral,hashedBonds);
    CHECK(solid&&hashed);if(solid&&hashed){std::array options{*hashed};auto set=solid->withIsomers(options);CHECK(set);if(set){auto m=buildIsomerMap(*set,0);CHECK(m&&m->wedges().size()==1&&m->wedges()[0].hashed);}}
    std::vector<Molecule> tooMany(4097,*ether);auto limit=ethanol->withIsomers(tooMany);CHECK(!limit&&limit.error().code==ErrorCode::limit_exceeded);
    MaterialCatalog catalog;
    MaterialRecord first{"test:ethanol","Ethanol","C2H6O","liquid",789,{},{},{},{},{},"test fixture"};
    MaterialRecord second{"test:ether","Ether","C2H6O","liquid",713,{},{},{},{},{},"test fixture"};
    CHECK(catalog.add(first)&&catalog.add(second));
    auto primary=catalog.resolve(*attached,"test:ethanol"),alternate=catalog.resolveIsomer(*attached,0,"test:ether");
    CHECK(primary&&alternate&&near(primary->physical.densityKgPerM3,789)&&near(alternate->physical.densityKgPerM3,713));
    CHECK(!catalog.resolveIsomer(*attached,0)&&!catalog.resolveIsomer(*attached,1,"test:ether"));
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--emit-benzene-svg") {
        std::vector<Atom> atoms(6);for(auto& a:atoms){a.element="C";a.implicitHydrogens=1;}
        std::vector<Bond> bonds;for(std::uint32_t i=0;i<6;++i)bonds.push_back({i,(i+1)%6,BondOrder::aromatic});
        auto m=Molecule::create(std::move(atoms),std::move(bonds),"C6H6");if(!m)return 2;
        auto map=buildMolecularMap(*m);if(!map)return 3;
        auto svg=molecularMapSvg(*map);if(!svg)return 4;
        std::cout<<*svg;return 0;
    }
    object();maps();isomers();return failures?1:0;
}
