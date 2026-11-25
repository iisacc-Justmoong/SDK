#pragma once

#include <string>
#include <utility>

namespace native_procedure_fixture {
/// An application-owned object with an ordinary source-level API.
class Application final {
public:
    /// Change the actual document title through the application's own method.
    void rename(std::string title) { title_ = std::move(title); }
    /// Read the current application title.
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
private:
    std::string title_ = "Draft";
};
}
