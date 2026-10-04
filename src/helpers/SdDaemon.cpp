#include "SdDaemon.hpp"
#include "memory/Memory.hpp"

#include <memory>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#ifdef _WIN32
// Windows has no systemd: not booted with it, and no notification socket.
int NSystemd::sdBooted() {
    return false;
}

int NSystemd::sdNotify(int unsetEnvironment, const char* state) {
    if (unsetEnvironment)
        unsetenv("NOTIFY_SOCKET");
    return 0;
}
#else
int NSystemd::sdBooted() {
    if (!faccessat(AT_FDCWD, "/run/systemd/system/", F_OK, AT_SYMLINK_NOFOLLOW))
        return true;

    if (errno == ENOENT)
        return false;

    return -errno;
}

int NSystemd::sdNotify(int unsetEnvironment, const char* state) {
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    constexpr char envVar[] = "NOTIFY_SOCKET";

    auto           cleanup = [unsetEnvironment, envVar](const int* fd) {
        if (unsetEnvironment)
            unsetenv(envVar);
        close(*fd);
    };
    std::unique_ptr<int, decltype(cleanup)> fdCleaup(&fd, cleanup);

    const char*                             addr = getenv(envVar);
    if (!addr)
        return 0;

    struct sockaddr_un unixAddr = {0};

    size_t             addrLen = strnlen(addr, sizeof(unixAddr.sun_path) - 1);

    unixAddr.sun_family = AF_UNIX;
    strncpy(unixAddr.sun_path, addr, addrLen);
    if (unixAddr.sun_path[0] == '@')
        unixAddr.sun_path[0] = '\0';

    if (connect(fd, rc<const sockaddr*>(&unixAddr), sizeof(struct sockaddr_un)) < 0)
        return -errno;

    // arbitrary value which seems to be enough for s-d messages
    ssize_t stateLen = strnlen(state, 128);
    if (write(fd, state, stateLen) == stateLen)
        return 1;

    return -errno;
}
#endif
