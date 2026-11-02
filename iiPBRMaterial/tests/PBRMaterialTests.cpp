#include "PBRMaterial.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace ii::pbr;
namespace {
int failures=0;
#define CHECK(x) do { if(!(x)) {std::cerr << __LINE__ << ": " #x "\n"; ++failures;} } while(false)
bool near(double a,double b,double tol=1e-8) {return std::abs(a-b)<=tol*std::max({1.0,std::abs(a),std::abs(b)});}
std::vector<std::byte> fixture() {
    std::ifstream f(IIPBR_FIXTURE,std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    std::vector<std::byte> out; for(unsigned char c:data)out.push_back(std::byte(c)); return out;
}
void formula() {
    auto water=parseMolecularFormula(" H2O "); CHECK(water); if(water) {
        CHECK(water->canonical=="H2O"&&water->atoms.at("H")==2&&water->atoms.at("O")==1);
        CHECK(water->molarMassGramsPerMol&&near(*water->molarMassGramsPerMol,18.015));
    }
    auto equivalent=parseMolecularFormula("OH2"); CHECK(equivalent&&equivalent->canonical=="H2O");
    auto subscript=parseMolecularFormula("H\xe2\x82\x82" "O");CHECK(subscript&&subscript->canonical=="H2O");
    auto hydrate=parseMolecularFormula("CuSO4\xC2\xB7" "5H2O"); CHECK(hydrate);
    if(hydrate) CHECK(hydrate->canonical=="CuH10O9S"&&hydrate->atoms.at("O")==9);
    auto complex=parseMolecularFormula("K4[Fe(CN)6]"); CHECK(complex&&complex->canonical=="C6FeK4N6");
    auto partial=parseMolecularFormula("XeF2");CHECK(partial&&!partial->molarMassGramsPerMol);
    for(auto input:{"","H0","Xx2","NaCl+","H2O.","13C","(H2O","H2O2.5"}) CHECK(!parseMolecularFormula(input));
    CHECK(!parseMolecularFormula("H1000000001"));
    CHECK(!parseMolecularFormula("H999999999H2"));
    CHECK(!parseMolecularFormula("((((((((((((((((((H))))))))))))))))))"));
}
void catalogue() {
    auto result=MaterialCatalog::referenceCatalog(); CHECK(result); if(!result)return;
    auto fe=result->resolve("Fe");CHECK(fe&&fe->physical.id=="nist:Fe"&&near(fe->physical.densityKgPerM3,7874));
    auto water=result->resolve("OH2");CHECK(water&&near(water->physical.densityKgPerM3,998.20675));
    CHECK(result->resolve("H2O",{})); // existing empty material-ID call remains unambiguous
    if(water)CHECK(water->physical.temperatureKelvin&&near(*water->physical.temperatureKelvin,293.15));
    CHECK(!result->resolve("C6H12O6"));
    MaterialRecord diamond{"test:diamond","Diamond","C","diamond",3510,{},{},{},{},{},"test fixture"};
    CHECK(result->add(diamond)); auto ambiguous=result->resolve("C"); CHECK(!ambiguous&&ambiguous.error().code==ErrorCode::ambiguous);
    auto candidates=result->candidates("C");CHECK(candidates&&candidates->size()==2);
    auto selected=result->resolve("C","test:diamond");CHECK(selected&&near(selected->physical.densityKgPerM3,3510));
    MaterialRecord steel{"test:steel","Steel","","alloy",7850,{},{},{},{},{},"test fixture"};
    CHECK(result->add(steel)); auto alloy=result->resolveId("test:steel"); CHECK(alloy&&!alloy->molecular);
    CHECK(!result->resolve("Fe","test:diamond"));CHECK(!result->add(diamond));
    diamond.id="bad";diamond.densityKgPerM3=0;CHECK(!result->add(diamond));
    diamond.densityKgPerM3=1;diamond.temperatureKelvin=-1;CHECK(!result->add(diamond));
}
void textures() {
    PpmDecoder decoder;auto bytes=fixture();CHECK(!bytes.empty());
    auto image=decoder.decode(bytes,"image/x-portable-pixmap");CHECK(image);if(!image)return;
    CHECK(image->width()==2&&image->height()==1);
    auto idmap=MaterialIdMap::create(*image,{{{255,0,0},"nist:Fe"},{{0,0,255},"nist:Al"}});
    CHECK(idmap); if(!idmap)return;
    CHECK(idmap->sample(0.25,0.5)&&*idmap->sample(0.25,0.5)=="nist:Fe");
    CHECK(idmap->sample(0.75,0.5)&&*idmap->sample(0.75,0.5)=="nist:Al");
    CHECK(idmap->sample(1.25,0.5,AddressMode::repeat)&&*idmap->sample(1.25,0.5,AddressMode::repeat)=="nist:Fe");
    CHECK(idmap->sample(1,0.5)&&*idmap->sample(1,0.5)=="nist:Al");
    CHECK(!idmap->sample(std::numeric_limits<double>::quiet_NaN(),0));
    CHECK(!MaterialIdMap::create(*image,{{{255,0,0},"nist:Fe"}}));
    CHECK(!MaterialIdMap::create(Texture{},{{{255,0,0},"nist:Fe"}}));
    auto truncated=bytes;truncated.pop_back();CHECK(!decoder.decode(truncated,"image/ppm"));
    CHECK(!decoder.decode(bytes,"image/png"));
    CHECK(!decoder.decode({},"image/ppm"));
    const std::string header="P6\n1 1\n255\n";
    std::vector<std::byte> withWhitespace; for(unsigned char c:header)withWhitespace.push_back(std::byte(c));
    withWhitespace.push_back(std::byte{32});withWhitespace.push_back(std::byte{10});withWhitespace.push_back(std::byte{3});
    auto whitespace=decoder.decode(withWhitespace,"image/ppm");
    CHECK(whitespace&&whitespace->pixels()[0].r==32&&whitespace->pixels()[0].g==10);
    std::array<Rgba8,1> color{{{128,64,0,128}}}, mr{{{0,64,128,255}}};
    auto c=Texture::create(1,1,color),m=Texture::create(1,1,mr);CHECK(c&&m);if(!c||!m)return;
    CHECK(!MaterialIdMap::create(*c,{{{128,64,0},"transparent"}}));
    SurfaceAppearance appearance;appearance.baseColorTexture=std::make_shared<Texture>(*c);
    appearance.metallicRoughnessTexture=std::make_shared<Texture>(*m);appearance.metallicFactor=0.5;appearance.roughnessFactor=0.8;
    auto s=sampleSurface(appearance,0,0);CHECK(s);if(s) {
        CHECK(near(s->baseColor.r,0.2158605001));CHECK(near(s->baseColor.a,128.0/255));
        CHECK(near(s->metallic,0.5*128/255));CHECK(near(s->roughness,0.8*64/255));
    }
}
}
int main(){formula();catalogue();textures();return failures?1:0;}
