// Windows implementation of src/init/initHelpers (Linux: uid checks, ambient
// capabilities, SCHED_RR via the kernel or rtkit).
#include "../../../src/init/initHelpers.hpp"
#include "../../../src/debug/log/Logger.hpp"

#include <windows.h>

// The Windows counterpart of running as root is an elevated token.
bool NInit::isSudo() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD           size     = 0;
    const bool      elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

// Capabilities do not exist on Windows; nothing is inherited by children.
void NInit::lowerAmbientCaps() {
    ;
}

// Raises the compositor thread like SCHED_RR's minimum priority would. Thread
// priorities are never inherited by spawned processes, matching SCHED_RESET_ON_FORK.
void NInit::gainRealTime() {
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
        LOG(Log::DEBUG, "Gained elevated thread priority");
    else
        LOG(Log::WARN, "Failed to gain elevated thread priority");
}
