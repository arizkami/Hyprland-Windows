#pragma once
// POSIX functions upstream Hyprland calls that the MSVC CRT lacks. Declared through
// Platform.hpp in every upstream translation unit; defined in Posix.cpp.
#ifdef _WIN32

#include <ctime>

#ifndef CLOCK_MONOTONIC
typedef int clockid_t;
// Same values as Linux, so protocol clock IDs (wp_presentation) keep their meaning.
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#endif

// CLOCK_MONOTONIC follows std::chrono::steady_clock and CLOCK_REALTIME follows
// std::chrono::system_clock, the pairing Hyprland's Time helpers assume.
int clock_gettime(clockid_t clock, timespec* out);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);

#ifndef SIGKILL
#define SIGKILL 9
#endif
// Only SIGKILL and SIGTERM are meaningful: both terminate the process.
int kill(pid_t pid, int signal);

#endif
