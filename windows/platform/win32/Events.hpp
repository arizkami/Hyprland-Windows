#pragma once

namespace WindowsPlatform {
    // Auto-reset Win32 event objects, for upstream code that signals the event loop
    // from other threads (a pipe write on Linux). Handles are opaque void*.
    void* createAutoResetEvent();
    void  setEvent(void* event);
    void  closeEvent(void* event);
}
