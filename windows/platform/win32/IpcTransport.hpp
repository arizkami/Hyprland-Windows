#pragma once
#include <windows.h>
#include <memory>
#include <string>

namespace WindowsIPC {
    constexpr size_t MAX_REQUEST = 4096;
    constexpr size_t MAX_REPLY   = 1024 * 1024;
    struct SHandleCloser {
        void operator()(void* handle) const;
    };
    using Handle = std::unique_ptr<void, SHandleCloser>;
    struct SReply {
        bool        success = false;
        std::string body;
        bool        changed = false, exit = false;
    };
    std::wstring logonSid();
    std::wstring pipeName();
    std::string  utf8(const std::wstring& text);
    std::wstring wide(const std::string& text);
    std::string  encodeReply(const SReply& reply);
    SReply       decodeReply(const std::string& reply);
    Handle       eventHandle();
    bool         finishIo(HANDLE handle, OVERLAPPED& operation, BOOL started, HANDLE stop, DWORD timeout, DWORD& transferred);
    SReply       request(const std::string& command);
}
