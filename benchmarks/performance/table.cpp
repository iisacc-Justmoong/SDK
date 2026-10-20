#include "backend/model/tablemodel.h"
#include <QCoreApplication>
#include <chrono>
#include <iostream>
int main(int argc,char**argv) {
    QCoreApplication app(argc,argv);
    TableModel model;
    QVariantList rows;
    for(int row=0;row<400;++row) {QVariantList cells;for(int c=0;c<8;++c)cells.append(QString::number(row*8+c));rows.append(QVariant(cells));}
    model.setRows(rows);
    const auto start=std::chrono::steady_clock::now();
    const auto cells=model.visibleCells();std::uint64_t sum=0;
    for(const auto& cell:cells)sum+=cell.toMap()["y"].toUInt();
    std::cout<<"table_visible_400x8 "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<' '<<sum<<'\n';
}
