#pragma once
#include "../../src/Layout.hpp"
#include <windows.h>
#include <map>
#include <string>
#include <vector>

namespace WindowsTiling {
    constexpr UINT TRACKER_MESSAGE   = WM_APP + 1;
    constexpr UINT LAYOUT_MESSAGE    = WM_APP + 2;
    constexpr UINT RECONCILE_MESSAGE = WM_APP + 3;
    struct SWindowRecord {
        WindowID        id   = 0;
        HWND            hwnd = nullptr, owner = nullptr;
        DWORD           process = 0, thread = 0;
        std::wstring    executable, title, windowClass;
        SRect           bounds;
        HMONITOR        monitor = nullptr;
        bool            visible = false, minimized = false, floating = true, userFloating = false, rejected = false;
        bool            hasOriginal = false, interacting = false;
        WINDOWPLACEMENT original{
            sizeof(WINDOWPLACEMENT),
        };
        std::optional<SRect> requested, actualAfterPlacement;
    };
    class CWindowManager {
      public:
        explicit CWindowManager(HWND panel, DWORD testProcess = 0);
        ~CWindowManager();
        bool                event(DWORD type, HWND window);
        void                setEnabled(bool enabled);
        bool                enabled() const;
        void                setLayout(ELayout layout);
        ELayout             layout() const;
        void                setGap(int gap);
        int                 gap() const;
        void                relayout();
        void                reconcile();
        void                toggleFloating();
        void                focus(EDirection direction);
        size_t              managedCount() const;
        size_t              trackedCount() const;
        const std::wstring& status() const;
        void                refreshAll();

      private:
        HWND                          m_panel       = nullptr;
        DWORD                         m_testProcess = 0;
        bool                          m_enabled     = false;
        ELayout                       m_layout      = ELayout::Dwindle;
        int                           m_gap         = 12;
        WindowID                      m_nextID      = 1;
        std::map<HWND, SWindowRecord> m_windows;
        std::vector<HWINEVENTHOOK>    m_hooks;
        std::wstring                  m_status;
        bool                          refresh(HWND window);
        bool                          restore(SWindowRecord& record);
        void                          restoreAll();
    };
}
