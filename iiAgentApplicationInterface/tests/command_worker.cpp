#include <chrono>
#include <thread>
// A disposable test child: the process owner must stop it before this wait ends.
int main() { std::this_thread::sleep_for(std::chrono::seconds(30)); }
