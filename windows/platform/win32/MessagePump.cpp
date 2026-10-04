#include "MessagePump.hpp"
#include <windows.h>

void WindowsPlatform::pumpThreadMessages(void*) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
