#pragma once
#include "IpcTransport.hpp"
#include <set>
#include <string>
#include <string_view>

namespace WindowsTiling {
    class CWindowManager;
}

namespace WindowsIPC {
    // Executes hyprctl requests ("[flags]/command args") against the window manager.
    // Runs on the UI thread only; the manager is not thread-safe.
    class CCommandHandler {
      public:
        explicit CCommandHandler(WindowsTiling::CWindowManager& manager);
        SReply execute(const std::string& request);

      private:
        WindowsTiling::CWindowManager&     m_manager;
        std::string                        m_splash;
        std::set<std::string, std::less<>> m_setByUser;
        SReply                             keyword(std::string_view name, std::string_view value);
        SReply                             getOption(std::string_view name, bool json) const;
        SReply                             dispatch(std::string_view dispatcher, std::string_view argument);
        SReply                             query(std::string_view command, std::string_view argument, bool json) const;
    };
}
