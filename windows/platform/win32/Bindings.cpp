#include "Bindings.hpp"
#include <array>

static constexpr std::array<WindowsBinds::SBind, 8> BINDS{{
    {'T', "T", "togglemanagement", "", "Toggle window management"},
    {'L', "L", "cyclelayout", "", "Switch between dwindle and master"},
    {'F', "F", "togglefloating", "", "Toggle floating for the active window"},
    {'Q', "Q", "exit", "", "Restore windows and exit"},
    {VK_LEFT, "left", "movefocus", "l", "Move focus left"},
    {VK_RIGHT, "right", "movefocus", "r", "Move focus right"},
    {VK_UP, "up", "movefocus", "u", "Move focus up"},
    {VK_DOWN, "down", "movefocus", "d", "Move focus down"},
}};

std::span<const WindowsBinds::SBind>                WindowsBinds::defaults() {
    return BINDS;
}
