#include "MaterialComposition.h"
#include "VisualTextureDecoder.h"
#include <BoxCad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

namespace pbr=ii::pbr;
namespace vol=ii::volumetric;
namespace app=ii::material_app;
struct TissueChange final:pbr::OrganicStateModel {
    pbr::Result<void> step(const pbr::OrganicMaterialDefinition&,pbr::OrganicStateData& state,double) const override {
        state.densityKgPerM3=1100;state.densitySource="fixture measurement";state.youngModulusPascal=20000;
        return {};
    }
};
int main() {
    std::ifstream file(IIPBR_PNG_FIXTURE,std::ios::binary);
    std::vector<char> input((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    if(input.empty())return 1;
    std::vector<std::byte> bytes; for(unsigned char c:input) bytes.push_back(std::byte(c));
    app::VisualTextureDecoder decoder;
    auto image=decoder.decode(bytes,"image/png");if(!image){std::cerr<<image.error().message<<'\n';return 2;}
    if(decoder.decode(bytes,"image/jpeg"))return 3;
    auto texture=pbr::MaterialIdMap::create(*image,{{{255,0,0},"nist:Fe"},{{0,0,255},"nist:Al"}});
    if(!texture)return 4;
    auto catalog=pbr::MaterialCatalog::referenceCatalog();if(!catalog)return 5;
    std::vector<app::Binding> bindings{{"nist:Fe","Fe",1,11},{"nist:Al","Al",2,22}};
    bindings[0].surface.baseColorTexture=std::make_shared<pbr::Texture>(*image);
    auto snapshots=app::resolveMaterials(*catalog,bindings);if(!snapshots)return 6;
    auto surface=app::sampleAppearance(*snapshots,11,0.25,0.5);
    if(!surface||surface->baseColor.r!=1||surface->baseColor.g!=0||surface->baseColor.b!=0)return 21;
    if(app::sampleAppearance(*snapshots,999,0,0))return 22;
    if(snapshots->materials[0].molecularFormula!="Fe"||snapshots->materials[0].sourceMaterialId!="nist:Fe"||
       snapshots->materials[0].densitySource.empty())return 7;
    auto bad=bindings;bad[0].molecularFormula="H2O";
    if(app::resolveMaterials(*catalog,bad))return 8;
    auto design=vol::BoxCad::create("mixed-block",1,{0,0,0},{1,1,1},11);if(!design)return 9;
    vol::BoxMesher base;app::TextureMappedMesher mapped(base,*texture,bindings,{});
    vol::MeshingOptions options;options.subdivisions={2,1,1};
    auto solid=vol::VolumetricSolidMesh::create(*design,mapped,snapshots->materials,snapshots->regions,options,{});
    if(!solid){std::cerr<<solid.error().message<<'\n';return 10;}
    const auto exact=solid->referenceMassProperties(),particles=solid->particleMassProperties();
    if(std::abs(exact.volume-1)>1e-10||std::abs(exact.mass-5286.5)>1e-7||std::abs(particles.mass-5286.5)>1e-7)return 11;
    if(!(exact.centerOfMass.x>0.25&&exact.centerOfMass.x<0.5))return 15;
    std::size_t iron=0,aluminium=0;
    for(const auto& p:solid->particles()) {
        if(p.material==1)++iron;else if(p.material==2)++aluminium;else return 12;
    }
    if(!iron||!aluminium)return 13;
    // Fixture values exercise propagation, not measured properties.
    pbr::MaterialRecord ethanolRecord{"test:ethanol","Ethanol","C2H6O","liquid",789,293.15,101325,12e9,0.3,100e6,"test fixture"};
    pbr::MaterialRecord etherRecord{"test:ether","Ether","C2H6O","liquid",713,293.15,101325,8e9,0.25,50e6,"test fixture"};
    if(!catalog->add(ethanolRecord)||!catalog->add(etherRecord))return 23;
    auto ethanol=pbr::Molecule::create({{"C",0,0,3},{"C",0,0,2},{"O",0,0,1}},{{0,1},{1,2}},"C2H6O");
    auto ether=pbr::Molecule::create({{"C",0,0,3},{"O"},{"C",0,0,3}},{{0,1},{1,2}},"C2H6O");
    if(!ethanol||!ether)return 24;
    std::array candidates{*ether,*ethanol};auto family=ethanol->withIsomers(candidates);if(!family)return 25;
    app::Binding selection{"test:ether","C2H6O",3,11};
    selection.surface.baseColorTexture=std::make_shared<pbr::Texture>(*image);
    auto applied=app::applyIsomerMaterial(*solid,*catalog,*family,0,selection);
    if(!applied){std::cerr<<applied.error().message<<'\n';return 26;}
    if(applied->isomer.atoms()[1].element!="O"||applied->isomerIndex!=0||applied->regionId!=11||!applied->surface.baseColorTexture)return 27;
    if(std::abs(applied->solid.referenceMassProperties().mass-1706)>1e-7||std::abs(applied->solid.particleMassProperties().mass-1706)>1e-7)return 28;
    if(std::abs(solid->referenceMassProperties().mass-5286.5)>1e-7)return 29;
    bool matchedMaterial=false;
    for(const auto& m:applied->solid.materials())if(m.id==3) {
        matchedMaterial=m.sourceMaterialId=="test:ether"&&m.molecularFormula=="C2H6O"&&m.densitySource=="test fixture"&&
            m.youngModulus&&*m.youngModulus==8e9&&m.poissonRatio&&*m.poissonRatio==0.25&&m.yieldStress&&*m.yieldStress==50e6&&
            m.referenceTemperatureKelvin&&*m.referenceTemperatureKelvin==293.15;
    }
    if(!matchedMaterial)return 30;
    for(const auto& particle:applied->solid.particles()) {
        if(particle.region==11&&(particle.material!=3||std::abs(particle.mass-713*particle.volume)>1e-9))return 31;
        if(particle.region==22&&(particle.material!=2||std::abs(particle.mass-2699*particle.volume)>1e-9))return 32;
    }
    auto switchBinding=selection;switchBinding.catalogId="test:ethanol";
    auto switched=app::applyIsomerMaterial(applied->solid,*catalog,*family,1,switchBinding);
    if(!switched||switched->isomer.atoms()[1].element!="C"||std::abs(switched->solid.referenceMassProperties().mass-1744)>1e-7||std::abs(applied->solid.referenceMassProperties().mass-1706)>1e-7)return 33;
    auto inferredFormula=selection;inferredFormula.molecularFormula.clear();
    auto fromStructure=app::applyIsomerMaterial(*solid,*catalog,*family,0,inferredFormula);
    if(!fromStructure||std::abs(fromStructure->solid.referenceMassProperties().mass-1706)>1e-7)return 41;
    auto invalidSelection=selection;invalidSelection.molecularFormula="H2O";
    if(app::applyIsomerMaterial(*solid,*catalog,*family,0,invalidSelection))return 34;
    invalidSelection=selection;invalidSelection.catalogId="nist:Fe";
    if(app::applyIsomerMaterial(*solid,*catalog,*family,0,invalidSelection))return 35;
    invalidSelection=selection;invalidSelection.catalogId="";
    if(app::applyIsomerMaterial(*solid,*catalog,*family,0,invalidSelection))return 36;
    invalidSelection=selection;invalidSelection.regionId=999;
    if(app::applyIsomerMaterial(*solid,*catalog,*family,0,invalidSelection))return 37;
    invalidSelection=selection;invalidSelection.materialId=2;
    if(app::applyIsomerMaterial(*solid,*catalog,*family,0,invalidSelection))return 38;
    if(app::applyIsomerMaterial(*solid,*catalog,*family,2,selection))return 39;
    if(std::abs(solid->referenceMassProperties().mass-5286.5)>1e-7)return 40;
    auto withoutCandidates=family->withIsomers({});if(!withoutCandidates)return 42;
    *family=std::move(*withoutCandidates);
    if(!family->isomers().empty()||applied->isomer.atoms()[1].element!="O"||switched->isomer.atoms()[1].element!="C")return 43;
    pbr::OrganicMaterialDefinition tissue;
    tissue.physical={"test:living-tissue","Synthetic tissue","","tissue",1050,293.15,101325,12000,0.4,3000,"test fixture"};
    tissue.surface.baseColorFactor={0.7,0.3,0.2,1};tissue.surface.metallicFactor=0;
    pbr::OrganicComponent cells;cells.id="cells";cells.name="Cells";cells.kind="cell";cells.massFraction=0.4;
    pbr::OrganicComponent matrix;matrix.id="matrix";matrix.name="Matrix";matrix.kind="extracellular_matrix";matrix.massFraction=0.6;
    tissue.components={cells,matrix};tissue.relations.push_back({"cells","matrix","embedded_in",{}});
    auto organic=pbr::OrganicMaterial::create(tissue);if(!organic)return 44;
    pbr::OrganicStateData initial;initial.temperatureKelvin=310;initial.hydrationMassFraction=0.7;
    initial.components.push_back({"cells",{{"population",500.0,"count","fixture"}}});
    auto baseline=organic->makeState(initial);if(!baseline)return 45;
    auto organicApplied=app::applyOrganicMaterial(*solid,*organic,*baseline,4,11);
    if(!organicApplied||std::abs(organicApplied->solid.referenceMassProperties().mass-1874.5)>1e-7||
       organicApplied->material.definition().components.size()!=2||organicApplied->surface.baseColorFactor.r!=0.7)return 46;
    TissueChange change;auto updatedState=organic->advance(*baseline,change,1);if(!updatedState||updatedState->data().timeSeconds!=1)return 47;
    auto organicUpdated=app::applyOrganicMaterial(organicApplied->solid,*organic,*updatedState,4,11);
    if(!organicUpdated||std::abs(organicUpdated->solid.referenceMassProperties().mass-1899.5)>1e-7||
       std::abs(organicUpdated->solid.particleMassProperties().mass-1899.5)>1e-7||
       std::abs(organicApplied->solid.referenceMassProperties().mass-1874.5)>1e-7)return 48;
    bool tissueFound=false;
    for(const auto& m:organicUpdated->solid.materials())if(m.id==4)
        tissueFound=m.sourceMaterialId=="test:living-tissue"&&m.densitySource=="fixture measurement"&&
                    !m.referenceTemperatureKelvin&&!m.referencePressurePascal&&m.youngModulus&&*m.youngModulus==20000;
    if(!tissueFound)return 49;
    auto growth=pbr::LogisticPopulationModel::create({"cells","population",0.1,1000});if(!growth)return 52;
    auto grown=organic->advance(*baseline,*growth,1);if(!grown)return 53;
    auto grownSolid=app::applyOrganicMaterial(*solid,*organic,*grown,4,11);
    if(!grownSolid||std::get<double>(grownSolid->state.data().components[0].properties[0].value)<=500||
       std::abs(grownSolid->solid.referenceMassProperties().mass-1874.5)>1e-7)return 54;
    if(app::applyOrganicMaterial(*solid,*organic,*baseline,2,11)||app::applyOrganicMaterial(*solid,*organic,*baseline,4,999))return 50;
    auto different=pbr::OrganicMaterial::create(tissue);if(!different||app::applyOrganicMaterial(*solid,*different,*baseline,4,11))return 51;
    pbr::CustomAtomDefinition customDefinition;customDefinition.id="test:atom";customDefinition.name="Synthetic atom";
    customDefinition.symbol="X";customDefinition.molarMassGramsPerMol=42.5;
    auto customType=pbr::CustomAtomType::create(customDefinition);if(!customType)return 55;
    auto customAtom=pbr::Atom::fromCustom(*customType,0,0,1);
    pbr::CustomBondDefinition customBond;customBond.id="test:link";customBond.name="Custom link";customBond.kind="user";
    customBond.direction=pbr::BondDirection::firstToSecond;customBond.first={pbr::BondEndpointKind::customAtom,"test:atom"};
    customBond.second={pbr::BondEndpointKind::element,"O"};customBond.dissociationEnergyJoulesPerMol=200000;customBond.energySource="test fixture";
    customBond.equilibriumLengthMetres=1e-10;customBond.stiffnessNewtonsPerMetre=100;
    auto customBondType=pbr::CustomBondType::create(customBond);if(!customBondType)return 66;
    auto customMolecule=pbr::Molecule::create({customAtom,{"O",0,0,1}},{pbr::Bond::fromCustom(0,1,*customBondType)},"H2O{test:atom}");
    auto customAlternative=pbr::Molecule::create({{"O",0,0,1},customAtom},{pbr::Bond::fromCustom(1,0,*customBondType)},"H2O{test:atom}");
    if(!customMolecule||!customAlternative)return 56;
    std::array customAlternatives{*customAlternative};auto customFamily=customMolecule->withIsomers(customAlternatives);if(!customFamily)return 57;
    pbr::MaterialRecord customRecord{"test:custom-material","Synthetic material","H2O{test:atom}","solid",1080,{},{},{},{},{},"test fixture"};
    std::array customTypes{*customType};if(!catalog->add(customRecord,customTypes))return 58;
    app::Binding customBinding{"test:custom-material","H2O{test:atom}",5,11};customBinding.customAtomTypes.assign(customTypes.begin(),customTypes.end());
    std::vector<app::Binding> customBindings{customBinding};auto customSnapshots=app::resolveMaterials(*catalog,customBindings);
    if(!customSnapshots||customSnapshots->materials[0].molecularFormula!="H2O{test:atom}")return 59;
    auto customApplied=app::applyIsomerMaterial(*solid,*catalog,*customFamily,0,customBinding);
    if(!customApplied||!customApplied->isomer.atoms()[1].custom||!customApplied->isomer.bonds()[0].custom||
       customApplied->isomer.bonds()[0].custom->definition().dissociationEnergyJoulesPerMol!=200000||
       std::abs(customApplied->solid.referenceMassProperties().mass-1889.5)>1e-7)return 60;
    pbr::OrganicMaterialDefinition customOrganic;customOrganic.physical=customRecord;
    customOrganic.customAtomTypes.assign(customTypes.begin(),customTypes.end());
    pbr::OrganicComponent customComponent;customComponent.id="custom-molecule";customComponent.name="Custom molecule";
    customComponent.kind="molecule";customComponent.molecularFormula="H2O{test:atom}";customComponent.molecule=*customMolecule;
    customOrganic.components.push_back(customComponent);
    auto customTissue=pbr::OrganicMaterial::create(customOrganic);if(!customTissue)return 61;
    auto customState=customTissue->makeState();if(!customState)return 62;
    auto customOrganicApplied=app::applyOrganicMaterial(*solid,*customTissue,*customState,5,11);
    if(!customOrganicApplied||std::abs(customOrganicApplied->solid.referenceMassProperties().mass-1889.5)>1e-7)return 63;
    auto conflictDefinition=customDefinition;conflictDefinition.molarMassGramsPerMol=43;
    auto conflictType=pbr::CustomAtomType::create(conflictDefinition);if(!conflictType)return 64;
    customBinding.customAtomTypes={*conflictType};
    if(app::applyIsomerMaterial(*solid,*catalog,*customFamily,0,customBinding))return 65;
    // With a coarse mesh the image boundary crosses tetrahedra. Centroid
    // assignment is a documented approximation, not an exact cut.
    auto coarse=options;coarse.subdivisions={1,1,1};
    auto coarseSolid=vol::VolumetricSolidMesh::create(*design,mapped,snapshots->materials,snapshots->regions,coarse,{});
    if(!coarseSolid||std::abs(coarseSolid->referenceMassProperties().mass-4424.0)>1e-7)return 16;
    // Alloys need an explicit sourced density, not a fabricated formula.
    pbr::MaterialRecord steel{"test:steel","Steel","","alloy",7850,{},{},{},{},{},"test fixture"};
    if(!catalog->add(steel))return 17;
    std::vector<app::Binding> mixed{{"test:steel","",3,33},{"nist:Al","Al",2,22}};
    auto mixSnapshots=app::resolveMaterials(*catalog,mixed);
    if(!mixSnapshots||!mixSnapshots->materials[0].molecularFormula.empty())return 18;
    auto mixTexture=pbr::MaterialIdMap::create(*image,{{{255,0,0},"test:steel"},{{0,0,255},"nist:Al"}});
    if(!mixTexture)return 19;
    app::TextureMappedMesher mixMesher(base,*mixTexture,mixed,{});
    auto alloySolid=vol::VolumetricSolidMesh::create(*design,mixMesher,mixSnapshots->materials,mixSnapshots->regions,options,{});
    if(!alloySolid||std::abs(alloySolid->referenceMassProperties().mass-5274.5)>1e-7)return 20;
    auto onlyIron=bindings;onlyIron.resize(1);
    app::TextureMappedMesher missing(base,*texture,onlyIron,{});
    if(missing.mesh(**design,options))return 14;
    std::cout<<"PNG ID map -> Fe/Al formula lookup -> mesh regions -> mass "<<exact.mass<<" kg\n";
}
