#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace WindowsTiling {
    using WindowID = uint64_t;
    struct SRect {
        int  x = 0, y = 0, width = 0, height = 0;
        bool operator==(const SRect&) const = default;
    };
    enum class ELayout {
        Dwindle,
        Master,
    };
    enum class EDirection {
        Left,
        Right,
        Up,
        Down,
    };
    struct SPlacement {
        WindowID id = 0;
        SRect    rect;
    };
    struct SWindowPolicy {
        bool ownProcess = false, shell = false, visible = false, tool = false, child = false, cloaked = false;
        bool owned = false, resizable = false, minimized = false;
    };
    enum class EDisposition {
        Ignore,
        Floating,
        Managed,
    };
    EDisposition            classify(const SWindowPolicy& policy);
    std::vector<SPlacement> arrange(const std::vector<WindowID>& ids, SRect area, ELayout layout, int innerGap, int outerGap);
    std::optional<WindowID> directionalFocus(const std::vector<SPlacement>& windows, WindowID from, EDirection direction);
}
