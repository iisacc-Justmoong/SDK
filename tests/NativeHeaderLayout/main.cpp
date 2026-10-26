#include <iiAccountManager/NativeAccount.h>
#include <iiLicenseManager/NativeLicenseClient.h>
#include <iiUpdateManager/NativeUpdateManager.h>
#include <iiFileProviderCompatibility.h>
#include <iiSDKCore/Runtime/EventLoop.h>

#include <stdexcept>

namespace {
void require(bool value) {
    if (!value)
        throw std::runtime_error("native header consumer contract failed");
}
}

int main() {
    using namespace iisacc::accounts::native;
    require(normalize_email("User@Example.com") == "user@example.com");
    require(valid_email("user@example.com"));
    require(!valid_email("invalid"));
    require(iisacc::updates::native::protocol_version("1.2") == "1.2.0");
    ii::runtime::EventLoop loop;
    iisacc::licensing::native::LicenseClient license(loop, "layout", {}, {});
    require(!license.verifySignature("invalid"));
    iisacc::updates::native::UpdateManager update(loop, "layout", "0.0.0");
    require(update.state() == iisacc::updates::native::UpdateManager::State::Idle);
    auto *volatile compatible_writer = &iiFileProvider::native::writeCompatible;
    require(compatible_writer != nullptr);
}
