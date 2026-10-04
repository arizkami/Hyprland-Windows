// Named-pipe IPC primitives (windows/platform/win32/ipc/Pipe.*) on the libwayland Win32 loop.
#include "../platform/win32/ipc/Pipe.hpp"
#include <wayland-server-core.h>
#include <windows.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

using namespace WindowsPlatform;
using namespace std::chrono_literals;

static std::wstring uniqueName(const char* suffix) {
    return hyprlandPipeName("test" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()), suffix);
}

// A client like hyprctl: send the request, read until the server closes.
static std::string request(const std::wstring& name, const std::string& text, bool sendRequest = true) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 100 && pipe == INVALID_HANDLE_VALUE; ++i) {
        pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            WaitNamedPipeW(name.c_str(), 50);
    }
    if (pipe == INVALID_HANDLE_VALUE)
        return "<connect failed>";
    DWORD written = 0;
    if (sendRequest)
        WriteFile(pipe, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    std::string reply;
    char        buffer[1024];
    DWORD       read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        reply.append(buffer, read);
    CloseHandle(pipe);
    return reply;
}

struct SServer {
    wl_event_loop*                                loop = wl_event_loop_create();
    std::vector<std::unique_ptr<CPipeConnection>> connections;
    std::unique_ptr<CPipeListener>                listener;
    std::vector<std::string>                      requests;
    std::vector<uint64_t>                         pids;
    std::string                                   replySuffix;

    explicit SServer(const std::wstring& name) {
        listener = std::make_unique<CPipeListener>(name, loop, [this](void* pipe) {
            connections.push_back(std::make_unique<CPipeConnection>(pipe, loop,
                                                                    CPipeConnection::SCallbacks{
                                                                        .readable = [this] { onRequest(); },
                                                                        .drained  = [] {},
                                                                        .closed   = [] {},
                                                                    }));
        });
    }

    void onRequest() {
        for (auto& connection : connections) {
            if (!connection->inputSize())
                continue;
            auto text = connection->takeInput();
            requests.push_back(text);
            pids.push_back(connection->clientPid());
            connection->write("reply:" + text + replySuffix);
        }
    }

    ~SServer() {
        connections.clear();
        listener.reset();
        wl_event_loop_destroy(loop);
    }

};

TEST(WindowsPipe, RequestReplyAndClientPid) {
    const auto name = uniqueName("socket");
    SServer    server(name);
    ASSERT_TRUE(server.listener->start());

    auto reply = std::async(std::launch::async, [name] { return request(name, "j/clients"); });
    // Reply, then close like REPLY_MODE_CLOSE; written data must stay readable.
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (server.requests.empty() && std::chrono::steady_clock::now() < deadline)
        wl_event_loop_dispatch(server.loop, 10);
    ASSERT_EQ(server.requests.size(), 1u);
    while (server.connections.front()->queued() && std::chrono::steady_clock::now() < deadline)
        wl_event_loop_dispatch(server.loop, 10);
    server.connections.front()->close();

    EXPECT_EQ(reply.get(), "reply:j/clients");
    EXPECT_EQ(server.requests.front(), "j/clients");
    EXPECT_EQ(server.pids.front(), GetCurrentProcessId());
}

TEST(WindowsPipe, LargeReplyAndConcurrentClients) {
    const auto name = uniqueName("socket");
    SServer    server(name);
    server.replySuffix = std::string(300 * 1024, 'x'); // larger than the pipe buffers
    ASSERT_TRUE(server.listener->start());

    std::vector<std::future<std::string>> replies;
    for (int i = 0; i < 6; ++i)
        replies.push_back(std::async(std::launch::async, [name, i] { return request(name, "client" + std::to_string(i)); }));

    // Close each connection once its whole reply is written (REPLY_MODE_CLOSE).
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    size_t     closed   = 0;
    while (closed < replies.size() && std::chrono::steady_clock::now() < deadline) {
        wl_event_loop_dispatch(server.loop, 10);
        closed = 0;
        for (auto& connection : server.connections) {
            if (connection->good() && connection->queued() == 0 && server.requests.size() == replies.size())
                connection->close();
            closed += connection->good() ? 0 : 1;
        }
    }

    ASSERT_EQ(server.requests.size(), replies.size());
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(replies[i].get(), "reply:client" + std::to_string(i) + server.replySuffix);
}

TEST(WindowsPipe, DisconnectIsReported) {
    const auto name   = uniqueName("socket2");
    bool       closed = false;
    auto       loop   = wl_event_loop_create();
    std::unique_ptr<CPipeConnection> connection;
    CPipeListener                    listener(name, loop, [&](void* pipe) {
        connection = std::make_unique<CPipeConnection>(pipe, loop, CPipeConnection::SCallbacks{.readable = [] {}, .drained = [] {}, .closed = [&] { closed = true; }});
    });
    ASSERT_TRUE(listener.start());

    HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!connection && std::chrono::steady_clock::now() < deadline)
        wl_event_loop_dispatch(loop, 10);
    ASSERT_TRUE(connection && connection->good());

    CloseHandle(client);
    while (!closed && std::chrono::steady_clock::now() < deadline)
        wl_event_loop_dispatch(loop, 10);
    EXPECT_TRUE(closed);
    EXPECT_FALSE(connection->good());
    EXPECT_FALSE(connection->write("late"));

    connection.reset();
    wl_event_loop_destroy(loop);
}
