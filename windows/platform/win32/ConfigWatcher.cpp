// Windows implementation of Config::CConfigWatcher's platform half (inotify on Linux).
//
// A worker thread waits on FindFirstChangeNotification handles for the parent
// directory of every watched file (and on watched directories themselves). When one
// fires it records the directory and signals an auto-reset event that the compositor's
// event loop waits on; onInotifyEvent() then runs on the loop thread and reports
// watched paths whose directory fired and whose last-write time changed (directories
// are reported whenever they fire).
#include "../../../src/config/shared/inotify/ConfigWatcher.hpp"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <thread>

using namespace Config;
namespace fs = std::filesystem;

struct CConfigWatcher::SWin32State {
    HANDLE                  changed = CreateEventW(nullptr, FALSE, FALSE, nullptr); // to the event loop
    HANDLE                  stop    = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE                  rewatch = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread             worker;

    std::mutex              mutex;           // guards directories and fired
    std::set<std::wstring>  directories;     // what the worker should watch
    std::set<std::wstring>  fired;           // directories with pending changes

    // Loop-thread state.
    std::map<std::string, fs::file_time_type> lastWrite;

    SWin32State() {
        worker = std::thread([this] { run(); });
    }

    ~SWin32State() {
        SetEvent(stop);
        if (worker.joinable())
            worker.join();
        CloseHandle(changed);
        CloseHandle(stop);
        CloseHandle(rewatch);
    }

    void run() {
        std::vector<HANDLE>       notifications;
        std::vector<std::wstring> watched;
        const auto                closeAll = [&] {
            for (auto handle : notifications)
                FindCloseChangeNotification(handle);
            notifications.clear();
            watched.clear();
        };

        for (;;) {
            std::vector<HANDLE> handles{stop, rewatch};
            handles.insert(handles.end(), notifications.begin(), notifications.end());
            const DWORD waited = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);

            if (waited == WAIT_OBJECT_0 || waited == WAIT_FAILED)
                break;

            if (waited == WAIT_OBJECT_0 + 1) {
                closeAll();
                std::lock_guard lock(mutex);
                for (const auto& directory : directories) {
                    // MAXIMUM_WAIT_OBJECTS bounds the set; config trees are far smaller.
                    if (notifications.size() + 2 >= MAXIMUM_WAIT_OBJECTS)
                        break;
                    const HANDLE handle = FindFirstChangeNotificationW(directory.c_str(), FALSE,
                                                                       FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
                    if (handle == INVALID_HANDLE_VALUE)
                        continue;
                    notifications.push_back(handle);
                    watched.push_back(directory);
                }
                continue;
            }

            const size_t index = waited - WAIT_OBJECT_0 - 2;
            if (index < notifications.size()) {
                {
                    std::lock_guard lock(mutex);
                    fired.insert(watched[index]);
                }
                FindNextChangeNotification(notifications[index]);
                SetEvent(changed);
            }
        }
        closeAll();
    }
};

static std::wstring directoryKey(const fs::path& path) {
    std::error_code ec;
    auto            absolute = fs::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal().wstring();
}

static std::optional<fs::file_time_type> lastWriteOf(const std::string& path) {
    std::error_code ec;
    const auto      time = fs::last_write_time(fs::u8path(path), ec);
    if (ec)
        return std::nullopt;
    return time;
}

CConfigWatcher::CConfigWatcher() : m_win32(makeUnique<SWin32State>()) {
    ;
}

void* CConfigWatcher::getChangeHandle() {
    return m_win32 ? m_win32->changed : nullptr;
}

void CConfigWatcher::setWatchList(const std::vector<std::string>& paths) {
    m_watches.clear();
    m_win32->lastWrite.clear();

    std::set<std::wstring> directories;
    for (const auto& path : paths) {
        const auto      fsPath = fs::u8path(path);
        std::error_code ec;
        const bool      isDirectory = fs::is_directory(fsPath, ec);
        directories.insert(directoryKey(isDirectory ? fsPath : fsPath.parent_path()));

        // Like the inotify version, a symlinked file is also watched at its target.
        std::error_code ec2, ec3;
        if (fs::is_symlink(fsPath, ec2) && !ec2) {
            const auto target = fs::canonical(fsPath, ec3);
            if (!ec3)
                directories.insert(directoryKey(target.parent_path()));
        }

        m_watches.emplace_back(SInotifyWatch{.wd = static_cast<int>(m_watches.size()), .file = path});
        if (const auto time = lastWriteOf(path))
            m_win32->lastWrite[path] = *time;
    }

    {
        std::lock_guard lock(m_win32->mutex);
        m_win32->directories = std::move(directories);
        m_win32->fired.clear();
    }
    SetEvent(m_win32->rewatch);
}

void CConfigWatcher::onInotifyEvent() {
    std::set<std::wstring> fired;
    {
        std::lock_guard lock(m_win32->mutex);
        fired.swap(m_win32->fired);
    }
    if (fired.empty() || !m_watchCallback)
        return;

    // Copy: the callback may reload the config and replace the watch list.
    const auto watches = m_watches;
    for (const auto& watch : watches) {
        const auto      fsPath = fs::u8path(watch.file);
        std::error_code ec;
        const bool      isDirectory = fs::is_directory(fsPath, ec);
        if (!fired.contains(directoryKey(isDirectory ? fsPath : fsPath.parent_path())))
            continue;

        if (!isDirectory) {
            const auto time     = lastWriteOf(watch.file);
            const auto previous = m_win32->lastWrite.find(watch.file);
            const bool existed  = previous != m_win32->lastWrite.end();
            if (time && existed && previous->second == *time)
                continue; // another file in the same directory changed
            if (!time && !existed)
                continue;
            if (time)
                m_win32->lastWrite[watch.file] = *time;
            else
                m_win32->lastWrite.erase(watch.file);
        }

        m_watchCallback(SConfigWatchEvent{.file = watch.file});
    }
}
