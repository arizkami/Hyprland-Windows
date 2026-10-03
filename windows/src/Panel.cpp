#include "Panel.hpp"
#include <yoga/Yoga.h>
#include <algorithm>
#include <array>

namespace WindowsUI {
    struct CPanel::SLayout {
        YGNodeRef                root = YGNodeNew();
        std::array<YGNodeRef, 8> nodes{};
        SLayout();
        ~SLayout();
    };

    CPanel::SLayout::SLayout() {
        YGNodeStyleSetFlexDirection(root, YGFlexDirectionColumn);
        YGNodeStyleSetPadding(root, YGEdgeAll, 18);
        YGNodeStyleSetGap(root, YGGutterAll, 10);
        for (size_t i = 0; i < nodes.size(); ++i) {
            nodes[i] = YGNodeNew();
            YGNodeStyleSetHeight(nodes[i], i == 0 ? 74.0F : i == 6 ? 92.0F : i == 7 ? 76.0F : 42.0F);
            YGNodeStyleSetFlexShrink(nodes[i], 0);
            YGNodeInsertChild(root, nodes[i], i);
        }
    }

    CPanel::SLayout::~SLayout() {
        YGNodeFreeRecursive(root);
    }
    CPanel::CPanel() : m_layout(std::make_unique<SLayout>()) {
        ;
    }
    CPanel::~CPanel() = default;

    void CPanel::layout(float width, float height, const SPanelState& state) {
        YGNodeStyleSetWidth(m_layout->root, width);
        YGNodeStyleSetHeight(m_layout->root, height);
        YGNodeCalculateLayout(m_layout->root, width, height, YGDirectionLTR);
        const SBox clip{
            0,
            0,
            width,
            height,
        };
        m_commands.clear();
        m_controls.clear();
        m_commands.push_back({
            clip,
            clip,
            L"",
            false,
            0,
        });
        const std::array<std::wstring, 8> labels{{
            L"HYPRLAND WINDOWS\nNative foundation · DWM desktop\nภาษาไทย · 日本語 · العربية",
            state.enabled ? L"Disable management and restore windows" : L"Enable desktop tiling (opt in)",
            state.master ? L"Layout: Master  →  switch to Dwindle" : L"Layout: Dwindle  →  switch to Master",
            L"−  Decrease gaps   (" + std::to_wstring(state.gap) + L" DIP)",
            L"+  Increase gaps   (" + std::to_wstring(state.gap) + L" DIP)",
            L"Restore windows and exit",
            std::wstring(state.enabled ? L"MANAGEMENT ENABLED" : L"MANAGEMENT DISABLED") + L"\nTracked: " + std::to_wstring(state.tracked) + L"   Tiled: " +
                std::to_wstring(state.managed) + L"\n" + state.status,
            state.hotkeyStatus.empty() ? L"Ctrl+Alt+T: toggle   L: layout   F: float\nCtrl+Alt+arrows: focus   Q: restore + exit\nTab / Enter: panel controls" : state.hotkeyStatus,
        }};
        const std::array<EAction, 8>      actions{{
            EAction::None,
            EAction::Toggle,
            EAction::Layout,
            EAction::GapDown,
            EAction::GapUp,
            EAction::Exit,
            EAction::None,
            EAction::None,
        }};
        size_t                            index = 0;
        for (size_t i = 0; i < m_layout->nodes.size(); ++i) {
            const auto node = m_layout->nodes[i];
            const SBox box{
                YGNodeLayoutGetLeft(node),
                YGNodeLayoutGetTop(node),
                YGNodeLayoutGetWidth(node),
                YGNodeLayoutGetHeight(node),
            };
            const bool interactive = actions[i] != EAction::None;
            m_commands.push_back({
                box,
                clip,
                labels[i],
                (i == 1 && state.enabled) || (interactive && index == m_focus),
                8,
            });
            if (interactive) {
                // Clipped portions of controls must not receive pointer input.
                m_controls.push_back({
                    box,
                    clip,
                    actions[i],
                });
                ++index;
            }
        }
    }
    const std::vector<SDrawCommand>& CPanel::commands() const {
        return m_commands;
    }
    EAction CPanel::hit(float x, float y) const {
        for (const auto& [box, clip, action] : m_controls) {
            if (x < clip.x || y < clip.y || x >= clip.x + clip.width || y >= clip.y + clip.height || x < box.x || y < box.y || x >= box.x + box.width || y >= box.y + box.height)
                continue;
            const float radius   = std::min({
                8.0F,
                box.width / 2,
                box.height / 2,
            });
            const float nearestX = std::clamp(x, box.x + radius, box.x + box.width - radius);
            const float nearestY = std::clamp(y, box.y + radius, box.y + box.height - radius);
            const float dx = x - nearestX, dy = y - nearestY;
            if (dx * dx + dy * dy <= radius * radius)
                return action;
        }
        return EAction::None;
    }
    void CPanel::nextFocus() {
        if (!m_controls.empty())
            m_focus = (m_focus + 1) % m_controls.size();
    }
    EAction CPanel::focusedAction() const {
        return m_controls.empty() ? EAction::None : m_controls[m_focus].action;
    }
}
