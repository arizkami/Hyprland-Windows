#include "Pipe.hpp"

#include <wayland-server-core.h>
#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

using namespace WindowsPlatform;

static std::atomic<uint64_t> s_nextConnectionId = 1;

namespace WindowsPlatform {
    int pipeConnectionSignaled(void*, void* data) {
        return static_cast<CPipeConnection*>(data)->onSignaled();
    }

    int pipeListenerSignaled(void*, void* data) {
        return static_cast<CPipeListener*>(data)->onSignaled();
    }
}

// Connection -------------------------------------------------------------------

struct CPipeConnection::SState {
    HANDLE                  pipe  = INVALID_HANDLE_VALUE;
    HANDLE                  event = CreateEventW(nullptr, TRUE, FALSE, nullptr); // read and write share it
    OVERLAPPED              readOp{};
    OVERLAPPED              writeOp{};
    bool                    reading = false, writing = false;
    std::array<char, 4096>  buffer{};
    std::string             input;
    std::string             output;
    size_t                  writeOffset = 0;
    size_t                  inFlight    = 0;
};

static void resignalCompleted(CPipeConnection::SState& state) {
    if ((state.reading && HasOverlappedIoCompleted(&state.readOp)) || (state.writing && HasOverlappedIoCompleted(&state.writeOp)))
        SetEvent(state.event);
}

CPipeConnection::CPipeConnection(void* pipe, wl_event_loop* loop, SCallbacks callbacks) : m_state(std::make_unique<SState>()), m_callbacks(std::move(callbacks)) {
    m_state->pipe          = pipe;
    m_state->readOp.hEvent = m_state->event;
    m_state->writeOp.hEvent = m_state->event;
    m_id                   = s_nextConnectionId++;

    ULONG pid = 0;
    if (GetNamedPipeClientProcessId(m_state->pipe, &pid))
        m_pid = pid;

    if (!m_state->event)
        return;
    m_source = wl_event_loop_add_handle(loop, m_state->event, pipeConnectionSignaled, this);
    m_good   = m_source && startRead();
}

CPipeConnection::~CPipeConnection() {
    close();
}

void CPipeConnection::close() {
    if (m_source) {
        wl_event_source_remove(m_source);
        m_source = nullptr;
    }
    if (m_state->pipe != INVALID_HANDLE_VALUE) {
        // Pending operations must finish before their buffers go away.
        CancelIoEx(m_state->pipe, nullptr);
        DWORD bytes = 0;
        if (m_state->reading)
            GetOverlappedResult(m_state->pipe, &m_state->readOp, &bytes, TRUE);
        if (m_state->writing)
            GetOverlappedResult(m_state->pipe, &m_state->writeOp, &bytes, TRUE);
        // CloseHandle, not DisconnectNamedPipe: the client may still read what was written.
        CloseHandle(m_state->pipe);
        m_state->pipe = INVALID_HANDLE_VALUE;
    }
    if (m_state->event) {
        CloseHandle(m_state->event);
        m_state->event = nullptr;
    }
    m_good = false;
}

bool CPipeConnection::good() const {
    return m_good;
}

uint64_t CPipeConnection::clientPid() const {
    return m_pid;
}

uint64_t CPipeConnection::id() const {
    return m_id;
}

std::string CPipeConnection::takeInput() {
    return std::move(m_state->input);
}

size_t CPipeConnection::inputSize() const {
    return m_state->input.size();
}

size_t CPipeConnection::queued() const {
    return m_state->output.size() - m_state->writeOffset;
}

// Starting an overlapped operation resets the shared event, which can swallow the
// completion signal of the other operation. Re-signal if either already finished.
static void resignalCompleted(CPipeConnection::SState& state);

bool CPipeConnection::startRead() {
    m_state->readOp.Offset = m_state->readOp.OffsetHigh = 0;
    if (ReadFile(m_state->pipe, m_state->buffer.data(), static_cast<DWORD>(m_state->buffer.size()), nullptr, &m_state->readOp) || GetLastError() == ERROR_IO_PENDING ||
        GetLastError() == ERROR_MORE_DATA) {
        // Completion (immediate or later) signals the event.
        m_state->reading = true;
        resignalCompleted(*m_state);
        return true;
    }
    return false;
}

bool CPipeConnection::startWrite() {
    const size_t remaining = m_state->output.size() - m_state->writeOffset;
    if (!remaining || m_state->writing)
        return true;
    m_state->inFlight = remaining;
    if (WriteFile(m_state->pipe, m_state->output.data() + m_state->writeOffset, static_cast<DWORD>(remaining), nullptr, &m_state->writeOp) || GetLastError() == ERROR_IO_PENDING) {
        m_state->writing = true;
        resignalCompleted(*m_state);
        return true;
    }
    return false;
}

bool CPipeConnection::write(std::string data) {
    if (!m_good)
        return false;
    if (m_state->writeOffset == m_state->output.size()) {
        m_state->output.clear();
        m_state->writeOffset = 0;
    }
    m_state->output += std::move(data);
    if (!startWrite()) {
        fail();
        return false;
    }
    return true;
}

void CPipeConnection::fail() {
    if (!m_good)
        return;
    m_good = false;
    if (m_callbacks.closed)
        m_callbacks.closed();
}

int CPipeConnection::onSignaled() {
    // Reset first: a completion after this point signals again.
    ResetEvent(m_state->event);
    DWORD bytes = 0;

    if (m_state->writing) {
        if (GetOverlappedResult(m_state->pipe, &m_state->writeOp, &bytes, FALSE)) {
            m_state->writing = false;
            m_state->writeOffset += bytes;
            if (m_state->writeOffset < m_state->output.size()) {
                if (!startWrite()) {
                    fail();
                    return 0;
                }
            } else if (m_callbacks.drained)
                m_callbacks.drained();
        } else if (GetLastError() != ERROR_IO_INCOMPLETE) {
            fail();
            return 0;
        }
    }
    if (!m_good)
        return 0;

    if (m_state->reading) {
        if (GetOverlappedResult(m_state->pipe, &m_state->readOp, &bytes, FALSE) || GetLastError() == ERROR_MORE_DATA) {
            m_state->reading = false;
            m_state->input.append(m_state->buffer.data(), bytes);

            // Drain what is already buffered, then report: the equivalent of reading a
            // non-blocking socket until EAGAIN.
            DWORD available = 0;
            while (PeekNamedPipe(m_state->pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
                DWORD      read   = 0;
                OVERLAPPED sync{};
                sync.hEvent       = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                const BOOL ok     = ReadFile(m_state->pipe, m_state->buffer.data(), static_cast<DWORD>(std::min<size_t>(available, m_state->buffer.size())), nullptr, &sync) ||
                    GetLastError() == ERROR_IO_PENDING;
                const BOOL done = ok && GetOverlappedResult(m_state->pipe, &sync, &read, TRUE);
                CloseHandle(sync.hEvent);
                if (!done)
                    break;
                m_state->input.append(m_state->buffer.data(), read);
            }

            if (m_callbacks.readable)
                m_callbacks.readable();
            if (m_good && !startRead())
                fail();
        } else if (GetLastError() != ERROR_IO_INCOMPLETE)
            fail(); // ERROR_BROKEN_PIPE: the client went away
    }
    return 0;
}

// Listener ---------------------------------------------------------------------

struct CPipeListener::SState {
    HANDLE     pipe  = INVALID_HANDLE_VALUE;
    HANDLE     event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED connectOp{};
    PSECURITY_DESCRIPTOR security = nullptr;
};

static std::wstring logonSidString() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
    std::vector<std::byte> buffer(size);
    std::wstring           result;
    if (size && GetTokenInformation(token, TokenGroups, buffer.data(), size, &size)) {
        const auto groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
        for (DWORD i = 0; i < groups->GroupCount && result.empty(); ++i) {
            if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
                continue;
            LPWSTR sid = nullptr;
            if (ConvertSidToStringSidW(groups->Groups[i].Sid, &sid)) {
                result = sid;
                LocalFree(sid);
            }
        }
    }
    CloseHandle(token);
    return result;
}

CPipeListener::CPipeListener(std::wstring name, wl_event_loop* loop, std::function<void(void* pipe)> onConnection) :
    m_state(std::make_unique<SState>()), m_name(std::move(name)), m_loop(loop), m_onConnection(std::move(onConnection)) {
    m_state->connectOp.hEvent = m_state->event;
}

CPipeListener::~CPipeListener() {
    if (m_source)
        wl_event_source_remove(m_source);
    if (m_state->pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_state->pipe, &m_state->connectOp);
        DWORD bytes = 0;
        GetOverlappedResult(m_state->pipe, &m_state->connectOp, &bytes, TRUE);
        CloseHandle(m_state->pipe);
    }
    if (m_state->event)
        CloseHandle(m_state->event);
    if (m_state->security)
        LocalFree(m_state->security);
}

const std::wstring& CPipeListener::name() const {
    return m_name;
}

bool CPipeListener::start() {
    const auto sid = logonSidString();
    if (sid.empty() || !m_state->event)
        return false;
    // Only the interactive logon session that started Hyprland may connect.
    const auto sddl = L"D:P(A;;GA;;;" + sid + L")";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &m_state->security, nullptr))
        return false;
    m_source = wl_event_loop_add_handle(m_loop, m_state->event, pipeListenerSignaled, this);
    return m_source && listen();
}

bool CPipeListener::listen() {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), m_state->security, FALSE};
    m_state->pipe = CreateNamedPipeW(m_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &attributes);
    if (m_state->pipe == INVALID_HANDLE_VALUE)
        return false;

    ResetEvent(m_state->event);
    if (ConnectNamedPipe(m_state->pipe, &m_state->connectOp))
        return true; // completed; the event is signaled
    switch (GetLastError()) {
        case ERROR_IO_PENDING: return true;
        case ERROR_PIPE_CONNECTED: SetEvent(m_state->event); return true;
        default: CloseHandle(m_state->pipe); m_state->pipe = INVALID_HANDLE_VALUE; return false;
    }
}

int CPipeListener::onSignaled() {
    DWORD bytes = 0;
    if (!GetOverlappedResult(m_state->pipe, &m_state->connectOp, &bytes, FALSE) && GetLastError() == ERROR_IO_INCOMPLETE)
        return 0;

    // Hand the connected instance over, then offer a fresh one for the next client.
    const HANDLE connected = m_state->pipe;
    m_state->pipe          = INVALID_HANDLE_VALUE;
    m_state->connectOp     = {};
    m_state->connectOp.hEvent = m_state->event;
    listen();
    m_onConnection(connected);
    return 0;
}

std::wstring WindowsPlatform::hyprlandPipeName(const std::string& instanceSignature, const std::string& socket) {
    const auto   text = "\\\\.\\pipe\\hypr\\" + instanceSignature + "\\" + socket;
    const int    size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
