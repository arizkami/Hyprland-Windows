#pragma once
#include <windows.h>
#include <cstdint>
#include <span>
#include <string_view>

namespace WindowsBinds {
    // Hyprland modmask bits (wlroots WLR_MODIFIER_*), reported by `hyprctl binds`.
    constexpr uint32_t MODMASK_CTRL = 1 << 2;
    constexpr uint32_t MODMASK_ALT  = 1 << 3;

    struct SBind {
        UINT             key = 0;
        std::string_view keyName, dispatcher, argument, description;
    };

    // Every bind is Ctrl+Alt+key. Hotkey IDs are the table index plus one.
    constexpr UINT         HOTKEY_MODIFIERS = MOD_CONTROL | MOD_ALT;
    constexpr uint32_t     MODMASK          = MODMASK_CTRL | MODMASK_ALT;
    std::span<const SBind> defaults();
}
