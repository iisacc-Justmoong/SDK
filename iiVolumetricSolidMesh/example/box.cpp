#include "BoxCad.h"
#include <iostream>
using namespace ii::volumetric;
int main() {
    auto design=BoxCad::create("example-box",1,{0,0,0},{0.2,0.1,0.05},1);
    if (!design) { std::cerr << design.error().message << '\n'; return 1; }
    BoxMesher mesher;
    auto object=VolumetricSolidMesh::create(*design,mesher,{{1,"aluminium",2700,{},{},{}}},{{1,1,"body"}});
    if (!object) { std::cerr << object.error().message << '\n'; return 1; }
    auto props=object->referenceMassProperties();
    std::cout << "mass_kg=" << props.mass << " particles=" << object->particles().size() << '\n';
}
