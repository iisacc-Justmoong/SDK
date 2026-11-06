#pragma once
#include <filesystem>
#include <functional>
#include <memory>

namespace iiGlobalSearch::detail {
// Watches creation, replacement and writes to one file. The callback only
// signals the caller; it must be thread-safe and must return promptly.
class DirectoryEvents final {
public:
    DirectoryEvents(const std::filesystem::path& directory, const std::filesystem::path& filename,
                    std::function<void()> notify);
    ~DirectoryEvents();
    DirectoryEvents(const DirectoryEvents&)=delete;
    DirectoryEvents& operator=(const DirectoryEvents&)=delete;
    void close() noexcept;
    [[nodiscard]] bool failed() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
