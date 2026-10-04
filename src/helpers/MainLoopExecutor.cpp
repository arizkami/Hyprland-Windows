#include "MainLoopExecutor.hpp"
#include "../managers/eventLoop/EventLoopManager.hpp"
#include "../macros.hpp"
#include <cstring>
#ifdef _WIN32
#include "../../windows/platform/win32/Events.hpp"
#else
#include <unistd.h>
#endif

#ifdef _WIN32
static int onSignaled(void* handle, void* data) {
    sc<CMainLoopExecutor*>(data)->onFired();
    return 0;
}

CMainLoopExecutor::CMainLoopExecutor(std::function<void()>&& callback) : m_fn(std::move(callback)) {
    m_signalEvent = WindowsPlatform::createAutoResetEvent();
    RASSERT(m_signalEvent, "CMainLoopExecutor: failed to create an event");

    m_event = wl_event_loop_add_handle(g_pEventLoopManager->m_wayland.loop, m_signalEvent, ::onSignaled, this);
}

CMainLoopExecutor::~CMainLoopExecutor() {
    if (m_event) // FIXME: potential race in case of a weird destroy on a worker thread
        wl_event_source_remove(m_event);
    WindowsPlatform::closeEvent(m_signalEvent);
}

void CMainLoopExecutor::signal() {
    WindowsPlatform::setEvent(m_signalEvent);
}
#else
static int onDataRead(int fd, uint32_t mask, void* data) {
    ((CMainLoopExecutor*)data)->onFired();
    return 0;
}

CMainLoopExecutor::CMainLoopExecutor(std::function<void()>&& callback) : m_fn(std::move(callback)) {

    int fds[2];
    RASSERT(pipe(fds) == 0, "CMainLoopExecutor: failed to open a pipe");

    m_event = wl_event_loop_add_fd(g_pEventLoopManager->m_wayland.loop, fds[0], WL_EVENT_READABLE, ::onDataRead, this);

    m_readFd  = Hyprutils::OS::CFileDescriptor(fds[0]);
    m_writeFd = Hyprutils::OS::CFileDescriptor(fds[1]);
}

CMainLoopExecutor::~CMainLoopExecutor() {
    if (m_event) // FIXME: potential race in case of a weird destroy on a worker thread
        wl_event_source_remove(m_event);
}

void CMainLoopExecutor::signal() {
    const char* amogus = "h";
    if (write(m_writeFd.get(), amogus, 1) < 0)
        LOG(Log::ERR, "signal: failed to write to fd {}: {}", m_writeFd.get(), strerror(errno));
}

#endif

void CMainLoopExecutor::onFired() {
    if (!m_fn)
        return;

    m_fn();
    m_fn = nullptr;

    // we need to remove the event here because we're on the main thread
    wl_event_source_remove(m_event);
    m_event = nullptr;

#ifndef _WIN32
    m_readFd.reset();
    m_writeFd.reset();
#endif
}
