#include "backend/hierarchy/hierarchyvisibility.h"
#include <chrono>
#include <iostream>
int main() {
    QVector<HierarchyNode> nodes;
    QSet<QString> expanded;
    for(int i=0;i<5000;++i) {auto id=QString::number(i);nodes.push_back({id,id,i ? QString::number(i-1) : QString{}, {}});expanded.insert(id);}
    const auto start=std::chrono::steady_clock::now();
    auto rows=HierarchyVisibility::project(nodes,expanded);
    std::uint64_t checksum=0;for(const auto& row:rows)checksum+=row.depth;
    std::cout<<"hierarchy_chain_5000 "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<' '<<checksum<<'\n';
}
