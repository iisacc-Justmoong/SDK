#include "PBRMaterial.h"
#include <iostream>
int main() {
    auto catalog=ii::pbr::MaterialCatalog::referenceCatalog(); if(!catalog)return 1;
    auto iron=catalog->resolve("Fe","nist:Fe"); if(!iron)return 1;
    if(!iron->molecular)return 1;
    std::cout << iron->molecular->canonical << " density=" << iron->physical.densityKgPerM3 << " kg/m3\n";
}
