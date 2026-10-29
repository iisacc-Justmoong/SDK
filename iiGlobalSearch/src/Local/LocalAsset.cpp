#include "GlobalSearch.h"
#include <DirectoryStorage.h>
#include <FileTree.h>
#include <iiFileProviderStorage.h>
#include <cerrno>
#include <iterator>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <winternl.h>
#include <limits>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace iiGlobalSearch {
namespace fs = std::filesystem;
namespace {
bool visible(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const auto& component : path) {
        const auto& name = component.native();
        if (name.empty() || component == "." || component == ".." || name.front() == fs::path::value_type('.') ||
            name.find(fs::path::value_type{}) != name.npos) return false;
#ifdef _WIN32
        if (name.find(L':') != name.npos) return false;
#endif
    }
    return true;
}
#ifdef _WIN32
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle() = default;
    void reset(HANDLE next) { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); value = next; }
};
bool safeKind(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))) return false;
    return bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
}
#else
struct Descriptor {
    int fd = -1;
    explicit Descriptor(int value = -1) : fd(value) {}
    ~Descriptor() { if (fd >= 0) ::close(fd); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    void reset(int value) { if (fd >= 0) ::close(fd); fd = value; }
};
bool sameTimes(const struct stat& before, const struct stat& after) {
#ifdef __APPLE__
    return before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec && before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
           before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec && before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec;
#else
    return before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
           before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
#endif
}
#endif
class LocalReader final : public AssetReader {
public:
    explicit LocalReader(fs::path root) : root_(std::move(root)) {
        iiSocietyContainer::FileTree tree(root_);
        if (!iiSocietyContainer::DirectoryStorage::isPackageDirectory(root_) || !tree.isValid() || tree.rootPath() != root_)
            throw std::invalid_argument("A canonical .societycontainer directory is required");
#ifdef _WIN32
        rootHandle_.reset(iiFileProvider::native::createFile(root_.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (rootHandle_.value == INVALID_HANDLE_VALUE || !safeKind(rootHandle_.value, true))
            throw std::runtime_error("Cannot pin a non-redirected container directory");
#else
        rootFd_.reset(iiFileProvider::native::openFile(root_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        struct stat held{}, named{};
        if (rootFd_.fd < 0 || ::fstat(rootFd_.fd, &held) || ::lstat(root_.c_str(), &named) ||
            held.st_dev != named.st_dev || held.st_ino != named.st_ino || !S_ISDIR(held.st_mode))
            throw std::runtime_error("Cannot pin the container directory");
#endif
    }
    std::expected<LoadedBytes, std::string> read(const AssetReference& reference, std::size_t max,
                                                std::stop_token stop) override {
        if (reference.kind != AssetSource::society || reference.container != root_ || !visible(reference.relativePath))
            return std::unexpected("Asset is outside the selected container");
#ifdef _WIN32
        Handle current; HANDLE parent = rootHandle_.value;
        std::size_t index = 0; const auto count = std::distance(reference.relativePath.begin(), reference.relativePath.end());
        for (const auto& part : reference.relativePath) {
            if (stop.stop_requested()) return std::unexpected("cancelled");
            const bool last = ++index == static_cast<std::size_t>(count);
            const auto& name = part.native();
            if (name.size() > std::numeric_limits<USHORT>::max() / sizeof(wchar_t)) return std::unexpected("Asset path component is too long");
            UNICODE_STRING nativeName{}; nativeName.Buffer = const_cast<PWSTR>(name.c_str());
            nativeName.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)); nativeName.MaximumLength = nativeName.Length;
            OBJECT_ATTRIBUTES attributes{}; attributes.Length = sizeof(attributes); attributes.RootDirectory = parent;
            attributes.ObjectName = &nativeName; attributes.Attributes = 0x00000040; // OBJ_CASE_INSENSITIVE
            IO_STATUS_BLOCK io{}; HANDLE next = INVALID_HANDLE_VALUE;
            constexpr ULONG openExisting = 1, synchronous = 0x00000020, openReparsePoint = 0x00200000;
            const ACCESS_MASK access = last ? GENERIC_READ | SYNCHRONIZE : FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE;
            const auto status = iiFileProvider::native::createFileNt(&next, access, &attributes, &io, nullptr,
                FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, openExisting,
                synchronous | openReparsePoint, nullptr, 0);
            if (status < 0 || next == INVALID_HANDLE_VALUE || !safeKind(next, !last)) {
                if (next != INVALID_HANDLE_VALUE && next) CloseHandle(next);
                return std::unexpected("Cannot open a non-redirected asset inside the container");
            }
            current.reset(next); parent = current.value;
        }
        BY_HANDLE_FILE_INFORMATION before{}, after{}; LARGE_INTEGER size{};
        if (!GetFileInformationByHandle(current.value, &before) || !GetFileSizeEx(current.value, &size) || size.QuadPart < 0)
            return std::unexpected("Cannot inspect asset");
        if (static_cast<std::uint64_t>(size.QuadPart) > max) return std::unexpected("Asset exceeds byte limit");
        LoadedBytes result; result.mimeType = reference.mimeType; result.bytes.reserve(static_cast<std::size_t>(size.QuadPart));
        std::uint8_t buffer[65536];
        for (;;) {
            if (stop.stop_requested()) return std::unexpected("cancelled");
            DWORD n = 0; if (!ReadFile(current.value, buffer, sizeof(buffer), &n, nullptr)) return std::unexpected("Asset read failed");
            if (!n) break;
            if (n > max - result.bytes.size()) return std::unexpected("Asset exceeds byte limit");
            result.bytes.insert(result.bytes.end(), buffer, buffer + n);
        }
        if (!GetFileInformationByHandle(current.value, &after) || before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
            before.nFileIndexHigh != after.nFileIndexHigh || before.nFileIndexLow != after.nFileIndexLow ||
            before.nFileSizeHigh != after.nFileSizeHigh || before.nFileSizeLow != after.nFileSizeLow ||
            before.ftLastWriteTime.dwHighDateTime != after.ftLastWriteTime.dwHighDateTime ||
            before.ftLastWriteTime.dwLowDateTime != after.ftLastWriteTime.dwLowDateTime || result.bytes.size() != static_cast<std::size_t>(size.QuadPart))
            return std::unexpected("Asset changed while being read");
        return result;
#else
        Descriptor current(::dup(rootFd_.fd));
        if (current.fd < 0) return std::unexpected("Cannot duplicate container handle");
        std::size_t index = 0; const auto count = std::distance(reference.relativePath.begin(), reference.relativePath.end());
        for (const auto& part : reference.relativePath) {
            if (stop.stop_requested()) return std::unexpected("cancelled");
            const bool last = ++index == static_cast<std::size_t>(count);
            // A replaced FIFO must not block before the regular-file check.
            int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (last ? O_NONBLOCK : O_DIRECTORY);
            const int next = iiFileProvider::native::openFileAt(current.fd, part.c_str(), flags);
            if (next < 0) return std::unexpected("Cannot open a non-redirected asset inside the container");
            current.reset(next);
        }
        struct stat before{}, after{};
        if (::fstat(current.fd, &before) || !S_ISREG(before.st_mode) || before.st_size < 0)
            return std::unexpected("Asset is not a regular file");
        if (static_cast<std::uintmax_t>(before.st_size) > max) return std::unexpected("Asset exceeds byte limit");
        LoadedBytes result; result.mimeType = reference.mimeType; result.bytes.reserve(static_cast<std::size_t>(before.st_size));
        std::uint8_t buffer[65536];
        for (;;) {
            if (stop.stop_requested()) return std::unexpected("cancelled");
            const auto n = ::read(current.fd, buffer, sizeof(buffer));
            if (n < 0) { if (errno == EINTR) continue; return std::unexpected("Asset read failed"); }
            if (!n) break;
            if (static_cast<std::size_t>(n) > max - result.bytes.size()) return std::unexpected("Asset exceeds byte limit");
            result.bytes.insert(result.bytes.end(), buffer, buffer + n);
        }
        if (::fstat(current.fd, &after) || before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
            before.st_size != after.st_size || !sameTimes(before, after) ||
            result.bytes.size() != static_cast<std::size_t>(after.st_size)) return std::unexpected("Asset changed while being read");
        return result;
#endif
    }
private:
    fs::path root_;
#ifdef _WIN32
    Handle rootHandle_;
#else
    Descriptor rootFd_;
#endif
};
}
std::shared_ptr<AssetReader> makeLocalAssetReader(const fs::path& root) { return std::make_shared<LocalReader>(root); }
} // namespace iiGlobalSearch
