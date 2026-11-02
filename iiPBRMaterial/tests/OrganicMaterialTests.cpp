#include "OrganicMaterial.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace ii::pbr;
namespace {
int failures=0;
#define CHECK(x) do { if(!(x)) {std::cerr<<__LINE__<<": " #x "\n";++failures;} } while(false)
bool near(double a,double b) {return std::abs(a-b)<1e-9*std::max({1.0,std::abs(a),std::abs(b)});}
OrganicMaterialDefinition fixture() {
    OrganicMaterialDefinition d;
    d.physical={"test:tissue","Synthetic tissue","","tissue",1050,293.15,101325,12000,0.4,3000,"test fixture"};
    d.surface.baseColorFactor={0.7,0.3,0.2,1};d.surface.metallicFactor=0;d.surface.roughnessFactor=0.8;
    d.optics=OrganicOpticalProfile{{0.1,0.2,0.3},{1.0,2.0,3.0},0.8,1.4};
    d.properties.push_back({"taxonomy",std::string("custom organism"),"","test fixture"});
    OrganicComponent body;body.id="body";body.name="Body";body.kind="organism";
    OrganicComponent water;water.id="water";water.name="Water";water.kind="molecule";water.parentId="body";
    water.molecularFormula="H2O";water.massFraction=0.7;water.volumeFraction=0.6;
    auto molecule=Molecule::create({{"O"},{"H"},{"H"}},{{0,1},{0,2}},"H2O");
    if(molecule)water.molecule=std::move(*molecule);
    OrganicComponent tissue;tissue.id="tissue";tissue.name="Tissue";tissue.kind="tissue";tissue.parentId="body";
    tissue.massFraction=0.3;tissue.volumeFraction=0.4;
    tissue.properties.push_back({"fiber_directions",std::vector<double>{1,0,0},"","test fixture"});
    OrganicComponent cell;cell.id="cell";cell.name="Cell population";cell.kind="cell";cell.parentId="tissue";cell.massFraction=0.8;
    OrganicComponent matrix;matrix.id="ecm";matrix.name="Extracellular matrix";matrix.kind="matrix";matrix.parentId="tissue";matrix.massFraction=0.2;
    d.components={std::move(body),std::move(water),std::move(tissue),std::move(cell),std::move(matrix)};
    d.relations.push_back({"cell","ecm","embedded_in",{{"note",std::string("synthetic"),"",""}}});
    return d;
}
struct Growth final:OrganicStateModel {
    Result<void> step(const OrganicMaterialDefinition&,OrganicStateData& state,double seconds) const override {
        state.densityKgPerM3=1100.0+seconds;state.densitySource="model output";
        state.densityReferenceTemperatureKelvin=310.0;state.hydrationMassFraction=0.6;
        state.properties.push_back({"growth_rate",seconds,"1/s","simulation"});return {};
    }
};
struct Failure final:OrganicStateModel {
    Result<void> step(const OrganicMaterialDefinition&,OrganicStateData& state,double) const override {
        state.densityKgPerM3=-1;return std::unexpected(Error{ErrorCode::backend_failure,"expected failure"});
    }
};
struct Invalid final:OrganicStateModel {
    Result<void> step(const OrganicMaterialDefinition&,OrganicStateData& state,double) const override {
        state.hydrationMassFraction=2;return {};
    }
};
struct Throwing final:OrganicStateModel {
    Result<void> step(const OrganicMaterialDefinition&,OrganicStateData&,double) const override {throw std::runtime_error("expected exception");}
};
void definitionAndValidation() {
    auto d=fixture();auto m=OrganicMaterial::create(d);CHECK(m);if(!m)return;
    CHECK(m->definition().components.size()==5&&m->definition().relations.size()==1);
    CHECK(m->definition().components[1].molecule&&m->definition().components[1].molecule->formula().canonical=="H2O");
    auto optical=uncollidedTransmittance(*d.optics,0.5);CHECK(optical);
    if(optical){CHECK(near((*optical)[0],std::exp(-0.55)));CHECK(near((*optical)[2],std::exp(-1.65)));}
    CHECK(!uncollidedTransmittance(*d.optics,-1));
    auto extreme=*d.optics;extreme.absorptionPerMetre[0]=std::numeric_limits<double>::max();extreme.scatteringPerMetre[0]=std::numeric_limits<double>::max();
    auto zeroDistance=uncollidedTransmittance(extreme,0),smallDistance=uncollidedTransmittance(extreme,1e-308);
    CHECK(zeroDistance&&(*zeroDistance)[0]==1);
    CHECK(smallDistance&&near((*smallDistance)[0],std::exp(-2*(std::numeric_limits<double>::max()*1e-308))));
    auto bad=d;bad.components[1].id="body";CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.components[1].parentId="missing";CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.components[0].parentId="cell";CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.components[1].massFraction=0.9;CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.components[1].volumeFraction=-0.1;CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.components[1].molecularFormula="CO2";CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.relations[0].to="unknown";CHECK(!OrganicMaterial::create(bad));
    auto cyclicRelations=d;cyclicRelations.relations.push_back({"ecm","cell","signals",{}});CHECK(OrganicMaterial::create(std::move(cyclicRelations)));
    bad=d;bad.optics->anisotropy=1.1;CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.optics->absorptionPerMetre[0]=-1;CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.properties.push_back({"bad",std::numeric_limits<double>::quiet_NaN(),"",""});CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.properties.push_back(bad.properties[0]);CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.physical.densityKgPerM3=0;CHECK(!OrganicMaterial::create(bad));
    bad=d;bad.surface.roughnessFactor=1.1;CHECK(!OrganicMaterial::create(bad));
    auto ethanol=Molecule::create({{"C",0,0,3},{"C",0,0,2},{"O",0,0,1}},{{0,1},{1,2}},"C2H6O");
    auto ether=Molecule::create({{"C",0,0,3},{"O"},{"C",0,0,3}},{{0,1},{1,2}},"C2H6O");
    CHECK(ethanol&&ether);if(ethanol&&ether) {
        std::array options{*ether};auto family=ethanol->withIsomers(options);CHECK(family);
        if(family) {auto selected=family->isomer(0);CHECK(selected);if(selected){auto withIsomer=d;
            OrganicComponent component;component.id="isomer";component.name="Isomer";component.kind="molecule";component.molecularFormula="C2H6O";component.molecule=*selected;
            withIsomer.components.push_back(std::move(component));auto organic=OrganicMaterial::create(std::move(withIsomer));
            CHECK(organic&&organic->definition().components.back().molecule->atoms()[1].element=="O");}}
    }
}
void stateAndModel() {
    auto material=OrganicMaterial::create(fixture());auto other=OrganicMaterial::create(fixture());CHECK(material&&other);if(!material||!other)return;
    OrganicStateData d;d.temperatureKelvin=310;d.pH=7.2;d.hydrationMassFraction=0.7;d.viabilityFraction=0.95;
    d.components.push_back({"cell",{{"population",std::int64_t(2000),"count","sample"}}});
    auto state=material->makeState(d);CHECK(state);if(!state)return;
    auto base=material->resolve(*state);CHECK(base&&near(base->physical.densityKgPerM3,1050));
    if(base)CHECK(base->physical.temperatureKelvin&&near(*base->physical.temperatureKelvin,293.15));
    CHECK(!other->resolve(*state));
    auto invalid=d;invalid.hydrationMassFraction=1.1;CHECK(!material->makeState(invalid));
    invalid=d;invalid.components[0].componentId="unknown";CHECK(!material->makeState(invalid));
    invalid=d;invalid.components.push_back(invalid.components[0]);CHECK(!material->makeState(invalid));
    invalid=d;invalid.densityKgPerM3=1100;CHECK(!material->makeState(invalid));
    invalid=d;invalid.timeSeconds=std::numeric_limits<double>::quiet_NaN();CHECK(!material->makeState(invalid));
    Growth growth;auto next=material->advance(*state,growth,2);CHECK(next);
    if(next) {
        CHECK(near(next->data().timeSeconds,2)&&near(*next->data().hydrationMassFraction,0.6));
        auto resolved=material->resolve(*next);CHECK(resolved);
        if(resolved)CHECK(near(resolved->physical.densityKgPerM3,1102)&&resolved->physical.densitySource=="model output"&&
                          resolved->physical.temperatureKelvin&&near(*resolved->physical.temperatureKelvin,310)&&!resolved->physical.pressurePascal);
        CHECK(near(state->data().timeSeconds,0)&&near(*state->data().hydrationMassFraction,0.7));
    }
    Failure failure;Invalid invalidModel;Throwing throwing;
    CHECK(!material->advance(*state,failure,1));CHECK(!material->advance(*state,invalidModel,1));
    CHECK(!material->advance(*state,throwing,1));CHECK(!material->advance(*state,growth,0));
    CHECK(!other->advance(*state,growth,1));CHECK(near(state->data().timeSeconds,0));
    OrganicStateData population;population.components.push_back({"cell",{{"population",2000.0,"count","user"}}});
    auto initial=material->makeState(population);
    auto logistic=LogisticPopulationModel::create({"cell","population",0.1,10000});
    CHECK(initial&&logistic);if(initial&&logistic) {
        auto evolved=material->advance(*initial,*logistic,10);CHECK(evolved);
        if(evolved){const auto value=std::get<double>(evolved->data().components[0].properties[0].value);
            CHECK(near(value,10000/(1+4*std::exp(-1.0)))&&near(evolved->data().timeSeconds,10));}
        CHECK(near(std::get<double>(initial->data().components[0].properties[0].value),2000));
        CHECK(!material->advance(*state,*logistic,1)); // missing double population
    }
    CHECK(!LogisticPopulationModel::create({"cell","population",-1,10000}));
    CHECK(!LogisticPopulationModel::create({"cell","population",0.1,0}));
    OrganicStateData extreme;extreme.components.push_back({"cell",{{"population",std::numeric_limits<double>::denorm_min(),"count","user"}}});
    auto tiny=material->makeState(extreme);auto zeroGrowth=LogisticPopulationModel::create({"cell","population",0,std::numeric_limits<double>::max()});
    auto rapidGrowth=LogisticPopulationModel::create({"cell","population",1,std::numeric_limits<double>::max()});
    CHECK(tiny&&zeroGrowth&&rapidGrowth);
    if(tiny&&zeroGrowth&&rapidGrowth) {
        auto same=material->advance(*tiny,*zeroGrowth,1);CHECK(same);
        if(same)CHECK(std::get<double>(same->data().components[0].properties[0].value)==std::numeric_limits<double>::denorm_min());
        auto grown=material->advance(*tiny,*rapidGrowth,1000);CHECK(grown);
        if(grown){const auto value=std::get<double>(grown->data().components[0].properties[0].value);CHECK(std::isfinite(value)&&value>0&&std::abs(std::log(value)-(std::log(std::numeric_limits<double>::denorm_min())+1000))<1e-8);}
    }
}
}
int main(){definitionAndValidation();stateAndModel();return failures?1:0;}
