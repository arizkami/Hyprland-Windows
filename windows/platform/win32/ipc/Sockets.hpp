#pragma once
// Windows transports for Hyprland's IPC sockets: named pipes
// \\.\pipe\hypr\<instance signature>\.socket.sock (requests, hyprctl) and
// \\.\pipe\hypr\<instance signature>\.socket2.sock (events). They replace the Unix
// socket implementations in src/ipc/s{1,2}/Unix.cpp with the same behavior.
#include "../../../../src/helpers/memory/Memory.hpp"

namespace IPC::Socket1 {
    class IImplementation;
    UP<IImplementation> makeWindowsImplementation();
}

namespace IPC::Socket2 {
    class IImplementation;
    UP<IImplementation> makeWindowsImplementation();
}
