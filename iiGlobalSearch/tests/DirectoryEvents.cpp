#include "../src/Web/DirectoryEvents.h"
#include "../src/Web/WebWait.h"
#include <iiFileProviderNative.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using namespace iiGlobalSearch::detail;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) try {
    check(argc==2,"missing resource directory");
    iiFileProvider::native::TemporaryDirectory temporary(argv[1]);
    WebWait wait; std::atomic<unsigned> notifications{0};
    DirectoryEvents watcher(temporary.path(),"DevToolsActivePort",[&] { ++notifications; wait.signal(); });
    std::this_thread::sleep_for(50ms);
    check(notifications.load()==0,"idle directory caused repeated events");
    const auto file=temporary.path()/"DevToolsActivePort";
    iiFileProvider::native::File::write(file,"1234\n");
    check(wait.wait({},std::chrono::steady_clock::now()+2s)==WebWake::signaled,"file creation did not wake watcher");
    while(wait.wait({},std::chrono::steady_clock::now()+50ms)==WebWake::signaled) {}
    iiFileProvider::native::File::write(file,"12345\n");
    check(wait.wait({},std::chrono::steady_clock::now()+2s)==WebWake::signaled,"file change did not wake watcher");
    watcher.close(); const auto closed=notifications.load();
    iiFileProvider::native::File::write(file,"123456\n");
    std::this_thread::sleep_for(50ms);
    check(notifications.load()==closed,"closed watcher delivered an event");
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
