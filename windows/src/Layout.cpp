// Split-axis and master/stack mathematics adapted from Hyprland's
// src/layout/algorithm/tiled/{dwindle,master} (BSD-3-Clause, see ../../LICENSE).
// Platform objects, global state and compositor policy are intentionally absent.
#include "Layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace WindowsTiling {
    EDisposition classify(const SWindowPolicy& p) {
        if (p.ownProcess || p.shell || !p.visible || p.tool || p.child || p.cloaked)
            return EDisposition::Ignore;
        if (p.owned || !p.resizable)
            return EDisposition::Floating;
        return EDisposition::Managed;
    }

    std::vector<SPlacement> arrange(const std::vector<WindowID>& ids, SRect area, ELayout layout, int innerGap, int outerGap) {
        std::vector<SPlacement> result;
        if (ids.empty() || area.width <= 0 || area.height <= 0)
            return result;
        const int outer = std::clamp(outerGap, 0, std::max(0, (std::min(area.width, area.height) - 1) / 2));
        area            = {
            area.x + outer,
            area.y + outer,
            area.width - 2 * outer,
            area.height - 2 * outer,
        };
        const int gap = std::max(0, innerGap);
        if (layout == ELayout::Dwindle) {
            // Stable insertion order splits the last leaf along its longer axis.
            for (size_t i = 0; i < ids.size(); ++i) {
                if (i + 1 == ids.size()) {
                    result.push_back({
                        ids[i],
                        area,
                    });
                    break;
                }
                const bool horizontal = area.width >= area.height;
                const int  size       = horizontal ? area.width : area.height;
                if (size < 2)
                    return {}; // Cannot fit nonempty disjoint cells: leave windows floating.
                const int g     = std::min(gap, size - 2);
                const int first = (size - g) / 2;
                SRect     leaf  = area;
                if (horizontal) {
                    leaf.width = first;
                    area.x += first + g;
                    area.width -= first + g;
                } else {
                    leaf.height = first;
                    area.y += first + g;
                    area.height -= first + g;
                }
                result.push_back({
                    ids[i],
                    leaf,
                });
            }
        } else {
            if (ids.size() == 1)
                return {
                    {
                        ids.front(),
                        area,
                    },
                };
            if (area.width < 2 || ids.size() - 1 > static_cast<size_t>(area.height))
                return {};
            const int g      = std::min(gap, area.width - 2);
            const int master = (area.width - g) * 3 / 5;
            result.push_back({
                ids.front(),
                {
                    area.x,
                    area.y,
                    std::max(1, master),
                    area.height,
                },
            });
            const int count       = static_cast<int>(ids.size() - 1);
            const int verticalGap = count > 1 ? std::min(gap, (area.height - count) / (count - 1)) : 0;
            const int available   = area.height - verticalGap * (count - 1);
            for (int i = 0; i < count; ++i) {
                const int top    = available * i / count;
                const int bottom = available * (i + 1) / count;
                result.push_back({
                    ids[static_cast<size_t>(i) + 1],
                    {
                        area.x + std::max(1, master) + g,
                        area.y + top + i * verticalGap,
                        area.width - std::max(1, master) - g,
                        bottom - top,
                    },
                });
            }
        }
        return result;
    }

    std::optional<WindowID> directionalFocus(const std::vector<SPlacement>& windows, WindowID from, EDirection direction) {
        const auto current = std::find_if(windows.begin(), windows.end(), [from](const auto& w) { return w.id == from; });
        if (current == windows.end())
            return std::nullopt;
        double                  best = std::numeric_limits<double>::max();
        std::optional<WindowID> target;
        for (const auto& w : windows) {
            if (w.id == from)
                continue;
            const double dx      = w.rect.x + w.rect.width / 2.0 - current->rect.x - current->rect.width / 2.0;
            const double dy      = w.rect.y + w.rect.height / 2.0 - current->rect.y - current->rect.height / 2.0;
            const double primary = direction == EDirection::Left ? -dx : direction == EDirection::Right ? dx : direction == EDirection::Up ? -dy : dy;
            if (primary <= 0)
                continue;
            const double secondary = (direction == EDirection::Left || direction == EDirection::Right) ? std::abs(dy) : std::abs(dx);
            const double score     = primary + secondary * 2;
            if (score < best) {
                best   = score;
                target = w.id;
            }
        }
        return target;
    }
}
