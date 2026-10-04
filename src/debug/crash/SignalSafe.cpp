#include "SignalSafe.hpp"

#ifndef __GLIBC__
#include <signal.h>
#endif
#include <cstring>
#ifdef _WIN32
#include <cstdlib>
#define environ _environ
#else
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace SignalSafe;

#ifndef _WIN32
// NOLINTNEXTLINE
extern "C" char** environ;
#endif

//
char const* SignalSafe::getenv(char const* name) {
    const size_t len = strlen(name);
    for (char** var = environ; *var != nullptr; var++) {
        if (strncmp(*var, name, len) == 0 && (*var)[len] == '=') {
            return (*var) + len + 1;
        }
    }
    return nullptr;
}

char const* SignalSafe::strsignal(int sig) {
#ifdef __GLIBC__
    return sigabbrev_np(sig);
#elif defined(__DragonFly__) || defined(__FreeBSD__)
    return sys_signame[sig];
#else
    return "unknown";
#endif
}
