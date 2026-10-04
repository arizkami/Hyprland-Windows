#include "IpcServer.hpp"
#include <sddl.h>
#include <thread>
#include <mutex>
#include <deque>
#include <array>
#include <stdexcept>

namespace WindowsIPC {
    struct SPending {
        std::string command;
        SReply      reply;
        Handle      ready    = eventHandle();
        bool        canceled = false;
    };
    struct CIpcServer::SImplementation {
        HWND                                  window = nullptr;
        Handle                                pipe, stop = eventHandle();
        std::thread                           worker;
        std::mutex                            mutex;
        std::deque<std::shared_ptr<SPending>> pending;
        explicit SImplementation(HWND target);
        ~SImplementation();
        void   serve();
        SReply dispatch(std::string command);
        bool   transfer(bool write, void* data, DWORD length, DWORD& bytes, DWORD timeout);
    };
    CIpcServer::SImplementation::SImplementation(HWND target) : window(target) {
        const auto           name       = pipeName();
        const auto           acl        = L"D:P(A;;GRGW;;;" + logonSid() + L")";
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
            throw std::runtime_error("Cannot create IPC security descriptor");
        SECURITY_ATTRIBUTES security{
            sizeof(SECURITY_ATTRIBUTES),
            descriptor,
            FALSE,
        };
        const auto handle =
            CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, static_cast<DWORD>(MAX_REQUEST), 0, &security);
        LocalFree(descriptor);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot start native IPC pipe");
        pipe.reset(handle);
        worker = std::thread([this] { serve(); });
    }
    CIpcServer::SImplementation::~SImplementation() {
        SetEvent(stop.get());
        CancelIoEx(pipe.get(), nullptr);
        if (worker.joinable())
            worker.join();
    }
    bool CIpcServer::SImplementation::transfer(bool write, void* data, DWORD length, DWORD& bytes, DWORD timeout) {
        auto       event = eventHandle();
        OVERLAPPED operation{};
        operation.hEvent   = event.get();
        const BOOL started = write ? WriteFile(pipe.get(), data, length, nullptr, &operation) : ReadFile(pipe.get(), data, length, nullptr, &operation);
        return finishIo(pipe.get(), operation, started, stop.get(), timeout, bytes);
    }
    SReply CIpcServer::SImplementation::dispatch(std::string command) {
        auto request     = std::make_shared<SPending>();
        request->command = std::move(command);
        {
            std::lock_guard lock(mutex);
            if (pending.size() >= 16)
                return {
                    false,
                    "IPC queue is full",
                };
            pending.push_back(request);
        }
        if (!PostMessageW(window, IPC_MESSAGE, 0, 0)) {
            // A later drain must not run a request whose failure was already reported.
            std::lock_guard lock(mutex);
            request->canceled = true;
            return {
                false,
                "Application is shutting down",
            };
        }
        const HANDLE events[]{
            stop.get(),
            request->ready.get(),
        };
        const DWORD     waited = WaitForMultipleObjects(2, events, FALSE, 3000);
        std::lock_guard lock(mutex);
        if (waited != WAIT_OBJECT_0 + 1) {
            request->canceled = true;
            return {
                false,
                "IPC dispatch timed out or stopped; query state before retrying",
            };
        }
        return request->reply;
    }
    void CIpcServer::SImplementation::serve() {
        try {
            while (WaitForSingleObject(stop.get(), 0) != WAIT_OBJECT_0) {
                auto       event = eventHandle();
                OVERLAPPED connection{};
                connection.hEvent   = event.get();
                DWORD       bytes   = 0;
                const BOOL  started = ConnectNamedPipe(pipe.get(), &connection);
                const DWORD error   = started ? ERROR_SUCCESS : GetLastError();
                if (error != ERROR_PIPE_CONNECTED && !finishIo(pipe.get(), connection, started, stop.get(), INFINITE, bytes))
                    break;
                std::array<char, MAX_REQUEST + 1> buffer{};
                SReply                            reply;
                if (transfer(false, buffer.data(), static_cast<DWORD>(buffer.size()), bytes, 3000)) {
                    if (!bytes || bytes > MAX_REQUEST)
                        reply = {
                            false,
                            "Request size must be 1–4096 bytes",
                        };
                    else
                        reply = dispatch(std::string(buffer.data(), bytes));
                } else
                    reply = {
                        false,
                        "Request read failed, exceeded the limit, or timed out",
                    };
                auto response = encodeReply(reply);
                if (response.size() > MAX_REPLY)
                    response = encodeReply({
                        false,
                        "Reply exceeds native IPC limit",
                    });
                if (transfer(true, response.data(), static_cast<DWORD>(response.size()), bytes, 3000)) {
                    char ack = 0;
                    transfer(false, &ack, 1, bytes, 3000); // Keep reply buffered until client consumes it.
                }
                DisconnectNamedPipe(pipe.get());
                if (reply.success && reply.exit)
                    PostMessageW(window, WM_CLOSE, 0, 0);
            }
        } catch (...) {
            // Never unwind through a background thread. Stop IPC without touching UI state.
            SetEvent(stop.get());
        }
    }
    CIpcServer::CIpcServer(HWND window) : m_impl(std::make_unique<SImplementation>(window)) {
        ;
    }
    CIpcServer::~CIpcServer() = default;
    void CIpcServer::drain(const std::function<SReply(const std::string&)>& handler) {
        std::deque<std::shared_ptr<SPending>> pending;
        {
            std::lock_guard lock(m_impl->mutex);
            pending.swap(m_impl->pending);
        }
        for (const auto& request : pending) {
            {
                std::lock_guard lock(m_impl->mutex);
                if (request->canceled)
                    continue;
            }
            SReply reply;
            try {
                reply = handler(request->command);
            } catch (const std::exception& error) {
                reply = {
                    false,
                    error.what(),
                };
            }
            {
                std::lock_guard lock(m_impl->mutex);
                request->reply = std::move(reply);
            }
            SetEvent(request->ready.get());
        }
    }
}
