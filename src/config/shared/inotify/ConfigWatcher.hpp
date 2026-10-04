#pragma once
#include "../../../helpers/memory/Memory.hpp"
#include <vector>
#include <string>
#include <functional>
#include <hyprutils/os/FileDescriptor.hpp>

namespace Config {
    class CConfigWatcher {
      public:
        CConfigWatcher();
        ~CConfigWatcher() = default;

        struct SConfigWatchEvent {
            std::string file;
        };

        void                            update();

        Hyprutils::OS::CFileDescriptor& getInotifyFD();
#ifdef _WIN32
        // Waitable event signalled when a watched file or directory may have changed;
        // onInotifyEvent() then reports the changes. Replaces the inotify fd on Windows.
        void* getChangeHandle();
#endif
        void                            setWatchList(const std::vector<std::string>& paths);
        void                            setOnChange(const std::function<void(const SConfigWatchEvent&)>& fn);
        void                            onInotifyEvent();

      private:
        struct SInotifyWatch {
            int         wd = -1;
            std::string file;
        };

        std::function<void(const SConfigWatchEvent&)> m_watchCallback;
        std::vector<SInotifyWatch>                    m_watches;
        Hyprutils::OS::CFileDescriptor                m_inotifyFd;
#ifdef _WIN32
        // Directory change notifications (windows/platform/win32/ConfigWatcher.cpp).
        struct SWin32State;
        UP<SWin32State> m_win32;
#endif
    };

    UP<CConfigWatcher>& watcher();
}
