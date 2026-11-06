#include "DirectoryEvents.h"
#include <atomic>
#include <cerrno>
#include <stdexcept>
#include <thread>
#include <utility>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace iiGlobalSearch::detail {
struct DirectoryEvents::Impl final {
    std::filesystem::path path;
    std::function<void()> notify;
    std::thread worker;
    std::atomic<bool> error{false};
#if defined(_WIN32)
    HANDLE directory=INVALID_HANDLE_VALUE, stopped=nullptr, ready=nullptr;
    OVERLAPPED operation{};
    alignas(DWORD) unsigned char buffer[16384]{};
    bool pending=false;
#elif defined(__APPLE__) || defined(__linux__)
    int source=-1, wakeRead=-1, wakeWrite=-1;
#if defined(__APPLE__)
    int dir=-1, file=-1;
#endif
#endif
    ~Impl() { close(); }
    void signal() noexcept {
        try { notify(); } catch (...) { error=true; }
    }
    void start(const std::filesystem::path& directoryPath, const std::filesystem::path& filename) {
        path=directoryPath/filename;
#if defined(_WIN32)
        directory=CreateFileW(directoryPath.c_str(),FILE_LIST_DIRECTORY,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr);
        stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(directory==INVALID_HANDLE_VALUE || !stopped || !ready) throw std::runtime_error("Cannot watch Chromium profile directory");
        operation.hEvent=ready;
        if(!arm()) throw std::runtime_error("Cannot arm Chromium profile watch");
#elif defined(__APPLE__) || defined(__linux__)
        int wake[2];
        if(::pipe(wake)!=0) throw std::runtime_error("Cannot create directory watch wake pipe");
        wakeRead=wake[0]; wakeWrite=wake[1];
        (void)::fcntl(wakeRead,F_SETFD,FD_CLOEXEC); (void)::fcntl(wakeWrite,F_SETFD,FD_CLOEXEC);
#if defined(__APPLE__)
        source=::kqueue(); dir=::open(directoryPath.c_str(),O_EVTONLY|O_CLOEXEC);
        if(source<0 || dir<0) throw std::runtime_error("Cannot watch Chromium profile directory");
        struct kevent changes[2];
        EV_SET(&changes[0],dir,EVFILT_VNODE,EV_ADD|EV_CLEAR,NOTE_WRITE|NOTE_DELETE|NOTE_RENAME,0,nullptr);
        EV_SET(&changes[1],wakeRead,EVFILT_READ,EV_ADD|EV_CLEAR,0,0,nullptr);
        if(::kevent(source,changes,2,nullptr,0,nullptr)<0) throw std::runtime_error("Cannot arm Chromium profile watch");
        watchFile();
#else
        source=::inotify_init1(IN_CLOEXEC|IN_NONBLOCK);
        if(source<0 || ::inotify_add_watch(source,directoryPath.c_str(),
            IN_CREATE|IN_CLOSE_WRITE|IN_MODIFY|IN_MOVED_TO|IN_ATTRIB|IN_DELETE_SELF|IN_MOVE_SELF)<0)
            throw std::runtime_error("Cannot watch Chromium profile directory");
#endif
#else
        (void)directoryPath; (void)filename;
        throw std::runtime_error("Native directory notifications are unavailable on this platform");
#endif
        worker=std::thread([this] { run(); });
    }
#if defined(_WIN32)
    bool arm() noexcept {
        ResetEvent(ready); operation.Internal=0; operation.InternalHigh=0;
        const BOOL ok=ReadDirectoryChangesW(directory,buffer,sizeof(buffer),FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE,
            nullptr,&operation,nullptr);
        pending=ok || GetLastError()==ERROR_IO_PENDING;
        return pending;
    }
#elif defined(__APPLE__)
    void watchFile() {
        const int candidate=::open(path.c_str(),O_EVTONLY|O_CLOEXEC);
        if(candidate<0) return;
        struct stat a{}, b{};
        if(file>=0 && ::fstat(file,&a)==0 && ::fstat(candidate,&b)==0 && a.st_dev==b.st_dev && a.st_ino==b.st_ino) {
            ::close(candidate); return;
        }
        struct kevent change;
        EV_SET(&change,candidate,EVFILT_VNODE,EV_ADD|EV_CLEAR,NOTE_WRITE|NOTE_EXTEND|NOTE_ATTRIB|NOTE_DELETE|NOTE_RENAME,0,nullptr);
        if(::kevent(source,&change,1,nullptr,0,nullptr)<0) { ::close(candidate); throw std::runtime_error("Cannot watch Chromium port file"); }
        if(file>=0) ::close(file);
        file=candidate;
    }
#endif
    void run() noexcept {
        try {
#if defined(_WIN32)
            HANDLE handles[]={stopped,ready};
            for(;;) {
                const auto result=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
                if(result==WAIT_OBJECT_0) break;
                if(result!=WAIT_OBJECT_0+1) throw std::runtime_error("Directory wait failed");
                DWORD bytes=0;
                if(!GetOverlappedResult(directory,&operation,&bytes,FALSE)) throw std::runtime_error("Directory watch failed");
                pending=false;
                if(!arm()) throw std::runtime_error("Directory rearm failed");
                signal();
            }
#elif defined(__APPLE__)
            for(;;) {
                struct kevent events[8]; const int count=::kevent(source,nullptr,0,events,8,nullptr);
                if(count<0 && errno==EINTR) continue;
                if(count<0) throw std::runtime_error("Directory wait failed");
                bool changed=false;
                for(int i=0;i<count;++i) {
                    if(static_cast<int>(events[i].ident)==wakeRead && events[i].filter==EVFILT_READ) return;
                    if(events[i].flags&EV_ERROR) throw std::runtime_error("Directory watch failed");
                    changed=true;
                }
                if(changed) { watchFile(); signal(); }
            }
#elif defined(__linux__)
            pollfd descriptors[]={{wakeRead,POLLIN,0},{source,POLLIN,0}};
            for(;;) {
                const int result=::poll(descriptors,2,-1);
                if(result<0 && errno==EINTR) continue;
                if(result<0 || (descriptors[1].revents&(POLLERR|POLLHUP|POLLNVAL))) throw std::runtime_error("Directory wait failed");
                if(descriptors[0].revents) return;
                if(descriptors[1].revents) {
                    char data[8192];
                    while(::read(source,data,sizeof(data))>0) {}
                    signal();
                }
            }
#endif
        } catch (...) { error=true; signal(); }
    }
    void close() noexcept {
        if(worker.joinable()) {
#if defined(_WIN32)
            SetEvent(stopped);
#elif defined(__APPLE__) || defined(__linux__)
            const char value=1; ssize_t sent;
            do { sent=::write(wakeWrite,&value,1); } while(sent<0 && errno==EINTR);
#endif
            worker.join();
        }
#if defined(_WIN32)
        if(pending && directory!=INVALID_HANDLE_VALUE) {
            CancelIoEx(directory,&operation); DWORD ignored=0;
            (void)GetOverlappedResult(directory,&operation,&ignored,TRUE); pending=false;
        }
        if(directory!=INVALID_HANDLE_VALUE) { CloseHandle(directory); directory=INVALID_HANDLE_VALUE; }
        if(ready) { CloseHandle(ready); ready=nullptr; }
        if(stopped) { CloseHandle(stopped); stopped=nullptr; }
#elif defined(__APPLE__) || defined(__linux__)
#if defined(__APPLE__)
        if(file>=0) { ::close(file); file=-1; }
        if(dir>=0) { ::close(dir); dir=-1; }
#endif
        if(source>=0) { ::close(source); source=-1; }
        if(wakeRead>=0) { ::close(wakeRead); wakeRead=-1; }
        if(wakeWrite>=0) { ::close(wakeWrite); wakeWrite=-1; }
#endif
    }
};
DirectoryEvents::DirectoryEvents(const std::filesystem::path& directory, const std::filesystem::path& filename,
                                 std::function<void()> notify) : impl_(std::make_unique<Impl>()) {
    if(!notify || filename.empty() || filename.has_parent_path()) throw std::invalid_argument("A directory, filename and notification are required");
    impl_->notify=std::move(notify); impl_->start(directory,filename);
}
DirectoryEvents::~DirectoryEvents()=default;
void DirectoryEvents::close() noexcept { impl_->close(); }
bool DirectoryEvents::failed() const noexcept { return impl_->error.load(); }
}
