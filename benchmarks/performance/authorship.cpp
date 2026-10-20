#include <iiFileProviderNative.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
int main() {
    using iiFileProvider::native::FileAuthor;
    auto author=FileAuthor::fromIisaccAccount({{"sub","subject"},{"email","writer@example.com"}},"https://iisacc.com","2026-10-14T10:00:00.000Z");
    if(!author) throw std::runtime_error("author");
    const auto data=author->toJsonText();
    const auto start=std::chrono::steady_clock::now();unsigned count=0;
    for(int i=0;i<2000;++i) count+=FileAuthor::fromJsonText(data).has_value();
    std::cout<<"authorship_parse_2000 "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<' '<<count<<'\n';
}
