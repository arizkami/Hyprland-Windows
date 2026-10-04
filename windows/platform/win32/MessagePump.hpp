#pragma once

namespace WindowsPlatform {
    // Dispatches every queued message on the calling thread: window procedures,
    // WinEvent hooks and low-level input hooks all run from the message queue.
    // Signature matches wl_event_loop_idle_func_t for wl_event_loop_add_win32_messages.
    void pumpThreadMessages(void* data);
}
