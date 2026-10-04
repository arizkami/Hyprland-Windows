#pragma once
// Named-pipe server primitives for Hyprland's IPC sockets on Windows (the Unix
// sockets of Linux). Overlapped I/O completes through a wl_event_loop handle source,
// so every callback runs on the compositor thread. No Win32 types in this header.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct wl_event_loop;
struct wl_event_source;

namespace WindowsPlatform {
    class CPipeConnection {
      public:
        struct SCallbacks {
            std::function<void()> readable; // bytes arrived; no more were immediately available
            std::function<void()> drained;  // everything queued has been written
            std::function<void()> closed;   // the client disconnected or I/O failed
        };

        // Takes ownership of a connected server pipe handle.
        CPipeConnection(void* pipe, wl_event_loop* loop, SCallbacks callbacks);
        ~CPipeConnection();

        CPipeConnection(const CPipeConnection&)            = delete;
        CPipeConnection& operator=(const CPipeConnection&) = delete;

        bool        good() const;
        uint64_t    clientPid() const;
        uint64_t    id() const;

        std::string takeInput();
        size_t      inputSize() const;

        // Queues bytes; returns false once the connection failed.
        bool   write(std::string data);
        size_t queued() const;

        // Ends the connection. Data already written stays readable by the client.
        void close();

      private:
        int  onSignaled();
        bool startRead();
        bool startWrite();
        void fail();

      public:
        struct SState;

      private:
        std::unique_ptr<SState> m_state;
        SCallbacks              m_callbacks;
        wl_event_source*        m_source = nullptr;
        uint64_t                m_id     = 0;
        uint64_t                m_pid    = 0;
        bool                    m_good   = false;

        friend int pipeConnectionSignaled(void*, void*);
    };

    class CPipeListener {
      public:
        // Accepts clients of `name` (\\.\pipe\...) from the current logon session only.
        CPipeListener(std::wstring name, wl_event_loop* loop, std::function<void(void* pipe)> onConnection);
        ~CPipeListener();

        CPipeListener(const CPipeListener&)            = delete;
        CPipeListener& operator=(const CPipeListener&) = delete;

        bool               start();
        const std::wstring& name() const;

      private:
        int  onSignaled();
        bool listen();

        struct SState;
        std::unique_ptr<SState>         m_state;
        std::wstring                    m_name;
        wl_event_loop*                  m_loop   = nullptr;
        wl_event_source*                m_source = nullptr;
        std::function<void(void* pipe)> m_onConnection;

        friend int pipeListenerSignaled(void*, void*);
    };

    // UTF-8 pipe name for an IPC socket of a Hyprland instance.
    std::wstring hyprlandPipeName(const std::string& instanceSignature, const std::string& socket);
}
