#pragma once
#include "IpcTransport.hpp"
#include <functional>
#include <memory>

namespace WindowsIPC {
    constexpr UINT IPC_MESSAGE = WM_APP + 4;
    class CIpcServer {
      public:
        explicit CIpcServer(HWND window);
        ~CIpcServer();
        void drain(const std::function<SReply(const std::string&)>& handler);

      private:
        struct SImplementation;
        std::unique_ptr<SImplementation> m_impl;
    };
}
