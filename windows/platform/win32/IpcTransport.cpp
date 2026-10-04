#include "IpcTransport.hpp"
#include <sddl.h>
#include <array>
#include <vector>
#include <stdexcept>

namespace WindowsIPC {
    void SHandleCloser::operator()(void* handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
    Handle eventHandle() {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event)
            throw std::runtime_error("Cannot create IPC event");
        return event;
    }
    std::wstring logonSid() {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
            throw std::runtime_error("Cannot query login token");
        Handle token(rawToken);
        DWORD  size = 0;
        GetTokenInformation(token.get(), TokenGroups, nullptr, 0, &size);
        if (!size)
            throw std::runtime_error("Cannot query login groups");
        std::vector<std::byte> buffer(size);
        if (!GetTokenInformation(token.get(), TokenGroups, buffer.data(), size, &size))
            throw std::runtime_error("Cannot read login groups");
        const auto groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
        for (DWORD i = 0; i < groups->GroupCount; ++i) {
            if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
                continue;
            LPWSTR sid = nullptr;
            if (!ConvertSidToStringSidW(groups->Groups[i].Sid, &sid))
                throw std::runtime_error("Cannot format login SID");
            const std::wstring result(sid);
            LocalFree(sid);
            return result;
        }
        throw std::runtime_error("No interactive login SID available");
    }
    std::wstring pipeName() {
        DWORD session = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session))
            throw std::runtime_error("Cannot query Windows session");
        return L"\\\\.\\pipe\\HyprlandWindows-" + std::to_wstring(session) + L"-" + logonSid();
    }
    std::string utf8(const std::wstring& text) {
        if (text.empty())
            return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (!size)
            throw std::runtime_error("Invalid UTF-16 text");
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
    std::wstring wide(const std::string& text) {
        if (text.empty())
            return {};
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (!size)
            throw std::runtime_error("Invalid UTF-8 request");
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }
    std::string encodeReply(const SReply& reply) {
        return std::string(reply.success ? "ok\n" : "error\n") + reply.body;
    }
    SReply decodeReply(const std::string& reply) {
        if (reply.starts_with("ok\n"))
            return {
                true,
                reply.substr(3),
            };
        if (reply.starts_with("error\n"))
            return {
                false,
                reply.substr(6),
            };
        throw std::runtime_error("Invalid Windows IPC reply");
    }
    bool finishIo(HANDLE handle, OVERLAPPED& operation, BOOL started, HANDLE stop, DWORD timeout, DWORD& transferred) {
        const DWORD error = started ? ERROR_SUCCESS : GetLastError();
        if (!started && error != ERROR_IO_PENDING)
            return false;
        if (!started) {
            const HANDLE events[]{
                stop ? stop : operation.hEvent,
                operation.hEvent,
            };
            const DWORD result    = WaitForMultipleObjects(stop ? 2 : 1, events, FALSE, timeout);
            const DWORD completed = stop ? WAIT_OBJECT_0 + 1 : WAIT_OBJECT_0;
            if (result != completed) {
                CancelIoEx(handle, &operation);
                GetOverlappedResult(handle, &operation, &transferred, TRUE);
                SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED);
                return false;
            }
        }
        return GetOverlappedResult(handle, &operation, &transferred, FALSE) != FALSE;
    }
    SReply request(const std::string& command) {
        if (command.empty() || command.size() > MAX_REQUEST || command.find('\0') != std::string::npos)
            throw std::runtime_error("Request must contain 1–4096 bytes without NUL");
        const auto name = pipeName();
        Handle     pipe;
        const auto deadline = GetTickCount64() + 2500;
        while (!pipe) {
            const auto handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                pipe.reset(handle);
                break;
            }
            if (GetLastError() != ERROR_PIPE_BUSY || GetTickCount64() >= deadline)
                throw std::runtime_error("Cannot connect to Hyprland Windows (not running, busy, or different login)");
            WaitNamedPipeW(name.c_str(), 250);
        }
        DWORD serverProcess = 0;
        if (GetNamedPipeServerProcessId(pipe.get(), &serverProcess))
            AllowSetForegroundWindow(serverProcess);
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr))
            throw std::runtime_error("Cannot set IPC message mode");
        auto       event = eventHandle();
        OVERLAPPED operation{};
        operation.hEvent  = event.get();
        DWORD transferred = 0;
        if (!finishIo(pipe.get(), operation, WriteFile(pipe.get(), command.data(), static_cast<DWORD>(command.size()), nullptr, &operation), nullptr, 2500, transferred))
            throw std::runtime_error("IPC write failed or timed out");
        ResetEvent(event.get());
        operation        = {};
        operation.hEvent = event.get();
        std::string response(MAX_REPLY, '\0');
        if (!finishIo(pipe.get(), operation, ReadFile(pipe.get(), response.data(), static_cast<DWORD>(response.size()), nullptr, &operation), nullptr, 5000, transferred))
            throw std::runtime_error("IPC reply failed or timed out; query state before retrying a mutation");
        response.resize(transferred);
        auto reply = decodeReply(response);
        ResetEvent(event.get());
        operation        = {};
        operation.hEvent = event.get();
        const char ack   = 'A';
        finishIo(pipe.get(), operation, WriteFile(pipe.get(), &ack, 1, nullptr, &operation), nullptr, 1000, transferred);
        return reply;
    }
}
