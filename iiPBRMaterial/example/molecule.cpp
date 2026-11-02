#include "MolecularMap.h"
#include <iostream>

// An application supplies connectivity. SVG stays in memory until the app
// chooses how to display it or persist it through its own file provider.
int main() {
    using namespace ii::pbr;
    auto water=Molecule::create({{"O"},{"H"},{"H"}},
                                {{0,1,BondOrder::single},{0,2,BondOrder::single}},"H2O");
    if(!water){std::cerr<<water.error().message<<'\n';return 1;}
    auto map=buildMolecularMap(*water);
    if(!map){std::cerr<<map.error().message<<'\n';return 1;}
    auto svg=molecularMapSvg(*map);
    if(!svg){std::cerr<<svg.error().message<<'\n';return 1;}
    std::cout<<*svg;
}
