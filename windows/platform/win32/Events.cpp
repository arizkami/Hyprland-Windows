#include "Events.hpp"
#include <windows.h>

void* WindowsPlatform::createAutoResetEvent() {
    return CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

void WindowsPlatform::setEvent(void* event) {
    if (event)
        SetEvent(event);
}

void WindowsPlatform::closeEvent(void* event) {
    if (event)
        CloseHandle(event);
}
