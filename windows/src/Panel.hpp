#pragma once
#include "Graphics.hpp"
#include <memory>

namespace WindowsUI {
    enum class EAction {
        None,
        Toggle,
        Layout,
        GapDown,
        GapUp,
        Exit,
    };
    struct SPanelState {
        bool         enabled = false, master = false;
        int          gap     = 12;
        size_t       tracked = 0, managed = 0;
        std::wstring status, hotkeyStatus;
    };
    class CPanel {
      public:
        CPanel();
        ~CPanel();
        void                             layout(float width, float height, const SPanelState& state);
        const std::vector<SDrawCommand>& commands() const;
        EAction                          hit(float x, float y) const;
        void                             nextFocus();
        EAction                          focusedAction() const;

      private:
        struct SLayout;
        std::unique_ptr<SLayout>  m_layout;
        std::vector<SDrawCommand> m_commands;
        struct SControl {
            SBox    box, clip;
            EAction action = EAction::None;
        };
        std::vector<SControl> m_controls;
        size_t                m_focus = 0;
    };
}
