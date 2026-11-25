#pragma once

#include <string>
#include <utility>

namespace external_host_fixture {
/// A live application object shared by its UI command and external procedures.
class Document final {
public:
    /// Replace the object's title using its ordinary application method.
    void rename(std::string value) { title_ = std::move(value); }
    /// Read the title directly from the live application instance.
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
private:
    std::string title_ = "initial";
};
}
