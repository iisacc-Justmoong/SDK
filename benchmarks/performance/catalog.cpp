#include "Parameters.h"
#include <QtCore/QCoreApplication>
#include <chrono>
#include <iostream>
int main(int argc,char**argv) {
    QCoreApplication app(argc,argv);
    QJsonArray fields;
    QJsonObject value;
    for(int i=0;i<512;++i) {auto name=QString("field_%1").arg(i);fields.append(QJsonObject{{"name",name},{"description","field"},{"native_type","string"},{"schema",QJsonObject{{"type","string"},{"pattern","^[a-z0-9]+$"}}}});value[name]="hello123";}
    const QJsonObject payload{{"name","payload"},{"description","nested fields"},{"native_type","object"},{"schema",QJsonObject{{"ref","fields"}}}};
    const QJsonArray groups{QJsonObject{{"id","fields"},{"parameters",fields}},QJsonObject{{"id","root"},{"parameters",QJsonArray{payload}}}};
    auto catalog=iiLocalLLM::ParameterCatalog::fromJson({{"schema_version",1},{"groups",groups}});
    const auto start=std::chrono::steady_clock::now();unsigned accepted=0;
    for(int i=0;i<400;++i) accepted+=catalog.validateValue("root","payload",value).isEmpty();
    std::cout<<"catalog_nested_512x400 "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<' '<<accepted<<'\n';
}
