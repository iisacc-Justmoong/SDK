#include <iiSDKCore>

// Confirm the Contracts target can consume both values and the Result API.
int main() {
    const ii::core::Result<int> result{42};
    return result.value_or(0) == 42 ? 0 : 1;
}
