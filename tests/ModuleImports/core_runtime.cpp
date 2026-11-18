#include <iiSDKCore>

// Exercise an exported runtime operation through the public import.
int main() {
    ii::runtime::EventLoop loop;
    bool called = false;
    if (!loop.post([&called] { called = true; })) {
        return 1;
    }
    (void)loop.poll();
    return called ? 0 : 1;
}
