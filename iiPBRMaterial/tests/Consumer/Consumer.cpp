#include <MolecularMap.h>
#include <PBRMaterial.h>
#include <OrganicMaterial.h>
#include <array>
int main() {
    using namespace ii::pbr;
    auto molecule=Molecule::create({{"O"},{"H"},{"H"}},{{0,1,BondOrder::single},{0,2,BondOrder::single}},"H2O");
    if(!molecule)return 1;
    auto map=buildMolecularMap(*molecule);if(!map)return 2;
    auto svg=molecularMapSvg(*map);if(!svg||svg->find("H2O molecular map")==std::string::npos)return 3;
    auto catalog=MaterialCatalog::referenceCatalog();if(!catalog||!catalog->resolve(*molecule,"nist:H2O:liquid:20C"))return 4;
    auto alternate=Molecule::create({{"O",0,0,2}}, {}, "H2O");if(!alternate)return 5;
    std::array alternatives{*alternate};
    auto family=Molecule::create({{"O"},{"H"},{"H"}},{{0,1,BondOrder::single},{0,2,BondOrder::single}},"H2O",alternatives);
    if(!family||family->isomers().size()!=1)return 6;
    auto selected=family->isomer(0);if(!selected||selected->atoms().size()!=1)return 7;
    auto selectedMap=buildIsomerMap(*family,0);if(!selectedMap||selectedMap->atoms().size()!=1)return 8;
    if(!catalog->resolveIsomer(*family,0,"nist:H2O:liquid:20C"))return 9;
    OrganicMaterialDefinition definition;
    definition.physical={"test:consumer-tissue","Tissue","","tissue",1000,{},{},{},{},{},"fixture"};
    OrganicComponent cells;cells.id="cells";cells.name="Cells";cells.kind="cell";definition.components.push_back(cells);
    auto organic=OrganicMaterial::create(std::move(definition));if(!organic)return 10;
    OrganicStateData input;input.components.push_back({"cells",{{"population",10.0,"count","fixture"}}});
    auto state=organic->makeState(input);auto model=LogisticPopulationModel::create({"cells","population",0.1,100});
    if(!state||!model)return 11;
    auto grown=organic->advance(*state,*model,1);if(!grown||!organic->resolve(*grown))return 12;
    CustomAtomDefinition custom;custom.id="test:atom";custom.symbol="X";custom.name="Custom";custom.molarMassGramsPerMol=10;
    auto type=CustomAtomType::create(custom);if(!type)return 13;
    auto customMolecule=Molecule::create({Atom::fromCustom(*type)}, {}, "{test:atom}");
    if(!customMolecule||!customMolecule->formula().molarMassGramsPerMol||*customMolecule->formula().molarMassGramsPerMol!=10)return 14;
    CustomBondDefinition bond;bond.id="test:bond";bond.name="Custom bond";bond.kind="user";
    bond.equilibriumLengthMetres=1e-10;bond.stiffnessNewtonsPerMetre=100;
    auto bondType=CustomBondType::create(bond);if(!bondType)return 15;
    auto pair=Molecule::create({Atom::fromCustom(*type),Atom::fromCustom(*type)},{Bond::fromCustom(0,1,*bondType)});
    HarmonicBondPotential harmonic;
    if(!pair||!evaluateBondPotential(*bondType,1.1e-10,harmonic)||!buildMolecularMap(*pair))return 16;
}
