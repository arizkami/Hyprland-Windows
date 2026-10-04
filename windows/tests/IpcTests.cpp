#include "../platform/win32/IpcServer.hpp"
#include <future>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace WindowsIPC;
static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
static void pump(CIpcServer& server) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == IPC_MESSAGE)
            server.drain([](const std::string& command) -> SReply {
                if (command.find('\0') != std::string::npos)
                    return {
                        false,
                        "NUL",
                    };
                wide(command);
                return {
                    true,
                    command,
                };
            });
        else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
}
static SReply await(std::future<SReply>& result, CIpcServer& server) {
    const auto deadline = GetTickCount64() + 10000;
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        require(GetTickCount64() < deadline, "IPC test deadline");
        pump(server);
    }
    return result.get();
}
static SReply rawRequest(const std::string& command) {
    Handle pipe(CreateFileW(pipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    require(pipe.get() != INVALID_HANDLE_VALUE, "Raw client connect");
    DWORD mode = PIPE_READMODE_MESSAGE, bytes = 0;
    require(SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr), "Raw message mode");
    require(WriteFile(pipe.get(), command.data(), static_cast<DWORD>(command.size()), &bytes, nullptr), "Raw write");
    // Echo replies carry the frame header on top of a maximum-size request.
    std::vector<char> output(MAX_REQUEST + 64);
    require(ReadFile(pipe.get(), output.data(), static_cast<DWORD>(output.size()), &bytes, nullptr), "Raw reply");
    auto       result = decodeReply(std::string(output.data(), bytes));
    const char ack    = 'A';
    WriteFile(pipe.get(), &ack, 1, &bytes, nullptr);
    return result;
}
int main() {
    HWND window = nullptr;
    try {
        require(wide(utf8(L"ไทย 😀")) == L"ไทย 😀", "Unicode round trip");
        require(decodeReply(encodeReply({
                                false,
                                "rejected",
                            }))
                        .body == "rejected",
                "Error frame round trip");
        bool rejected = false;
        try {
            decodeReply("unknown");
        } catch (const std::exception&) { rejected = true; }
        require(rejected, "Malformed reply rejected");
        window = CreateWindowExW(0, L"STATIC", L"IPC test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Create message-only target");
        auto                             server = std::make_unique<CIpcServer>(window);
        std::vector<std::future<SReply>> requests;
        for (int i = 0; i < 4; ++i)
            requests.push_back(std::async(std::launch::async, [i] { return request("client " + std::to_string(i)); }));
        for (size_t i = 0; i < requests.size(); ++i)
            require(await(requests[i], *server).body == "client " + std::to_string(i), "Concurrent client correlation");
        for (const auto& command : {std::string(MAX_REQUEST, 'x'), std::string(MAX_REQUEST + 1, 'x'), std::string("a\0b", 3), std::string("\xff", 1)}) {
            auto result = std::async(std::launch::async, [command] { return rawRequest(command); });
            require(await(result, *server).success == (command.size() == MAX_REQUEST), "Request limits and invalid encoding");
        }
        // Connect without sending. The server must cancel pending I/O promptly on shutdown.
        Handle stalled(CreateFileW(pipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        require(stalled.get() != INVALID_HANDLE_VALUE, "Stalled client connect");
        Sleep(40);
        const auto before = GetTickCount64();
        server.reset();
        require(GetTickCount64() - before < 1000, "Shutdown cancels stalled pipe read");
        stalled.reset();
        server              = std::make_unique<CIpcServer>(window);
        auto       waiting  = std::async(std::launch::async, [] {
            try {
                return request("pending");
            } catch (const std::exception& error) {
                return SReply{
                    false,
                    error.what(),
                };
            }
        });
        const auto deadline = GetTickCount64() + 2000;
        MSG        message{};
        while (!PeekMessageW(&message, window, IPC_MESSAGE, IPC_MESSAGE, PM_REMOVE)) {
            require(GetTickCount64() < deadline, "Pending dispatch reached UI");
            Sleep(10);
        }
        const auto pendingBefore = GetTickCount64();
        server.reset();
        require(GetTickCount64() - pendingBefore < 1000, "Shutdown cancels undrained UI dispatch");
        require(!waiting.get().success, "Interrupted request must fail");
        DestroyWindow(window);
        std::cout << "Native IPC encoding, bounds, concurrent clients and shutdown passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (window)
            DestroyWindow(window);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
