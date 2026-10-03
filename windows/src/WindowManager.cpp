#include "WindowManager.hpp"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <functional>

// Hooks are out-of-context and dispatched on the registering UI thread.
// Win32's message queue is the event queue; no inspection/placement in callbacks.
static HWND          s_dispatcher = nullptr;
static void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (!s_dispatcher || !window || (event >= EVENT_OBJECT_CREATE && (object != OBJID_WINDOW || child != CHILDID_SELF)))
        return;
    PostMessageW(s_dispatcher, WindowsTiling::TRACKER_MESSAGE, event, reinterpret_cast<LPARAM>(window));
}

static WindowsTiling::SRect windowBounds(HWND window) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        GetWindowRect(window, &rect);
    return {
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
    };
}

static bool closeBounds(const WindowsTiling::SRect& a, const WindowsTiling::SRect& b) {
    return std::abs(a.x - b.x) <= 2 && std::abs(a.y - b.y) <= 2 && std::abs(a.width - b.width) <= 2 && std::abs(a.height - b.height) <= 2;
}

static std::wstring executablePath(DWORD pid) {
    // HANDLE is required by the Win32 ABI; RAII owns process handles.
    const auto                              closer = [](void* handle) { CloseHandle(handle); };
    std::unique_ptr<void, decltype(closer)> process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), closer);
    if (!process)
        return {};
    std::array<wchar_t, 32768> path{};
    DWORD                      size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size))
        return {};
    return {path.data(), size};
}

namespace WindowsTiling {
    CWindowManager::CWindowManager(HWND panel, DWORD testProcess) : m_panel(panel), m_testProcess(testProcess) {
        s_dispatcher = panel;
        const std::array<std::pair<DWORD, DWORD>, 4> ranges{{
            {
                EVENT_OBJECT_CREATE,
                EVENT_OBJECT_NAMECHANGE,
            },
            {
                EVENT_SYSTEM_FOREGROUND,
                EVENT_SYSTEM_FOREGROUND,
            },
            {
                EVENT_SYSTEM_MINIMIZESTART,
                EVENT_SYSTEM_MINIMIZEEND,
            },
            {
                EVENT_SYSTEM_MOVESIZESTART,
                EVENT_SYSTEM_MOVESIZEEND,
            },
        }};
        for (const auto& range : ranges) {
            const auto hook = SetWinEventHook(range.first, range.second, nullptr, windowEvent, m_testProcess, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (!hook) {
                for (const auto registered : m_hooks)
                    UnhookWinEvent(registered);
                s_dispatcher = nullptr;
                throw std::runtime_error("SetWinEventHook failed; management unavailable");
            }
            m_hooks.push_back(hook);
        }
        refreshAll();
    }

    CWindowManager::~CWindowManager() {
        s_dispatcher = nullptr;
        for (const auto hook : m_hooks)
            UnhookWinEvent(hook);
        restoreAll();
    }

    bool CWindowManager::refresh(HWND window) {
        if (!IsWindow(window))
            return m_windows.erase(window) != 0;
        DWORD                    pid    = 0;
        const DWORD              thread = GetWindowThreadProcessId(window, &pid);
        std::array<wchar_t, 512> title{}, windowClass{};
        GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        GetClassNameW(window, windowClass.data(), static_cast<int>(windowClass.size()));
        const auto style   = GetWindowLongPtrW(window, GWL_STYLE);
        const auto exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        const HWND owner   = GetWindow(window, GW_OWNER);
        DWORD      cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        const std::wstring name = windowClass.data();
        const bool shell = window == GetShellWindow() || window == GetDesktopWindow() || name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd" || name == L"Progman" ||
            name == L"WorkerW" || name == L"XamlExplorerHostIslandWindow";
        const SWindowPolicy policy{
            pid == GetCurrentProcessId() || (m_testProcess && pid != m_testProcess),
            shell,
            IsWindowVisible(window) != FALSE,
            (exStyle & WS_EX_TOOLWINDOW) != 0,
            (style & WS_CHILD) != 0,
            cloaked != 0,
            owner != nullptr,
            (style & WS_THICKFRAME) != 0,
            IsIconic(window) != FALSE,
        };
        const auto disposition = classify(policy);
        auto       existing    = m_windows.find(window);
        if (existing != m_windows.end() && (existing->second.process != pid || existing->second.thread != thread || existing->second.windowClass != name)) {
            m_windows.erase(existing); // Do not restore placement into a reused HWND.
            existing = m_windows.end();
        }
        if (disposition == EDisposition::Ignore) {
            if (existing == m_windows.end())
                return false;
            // Restore hidden/cloaked survivors before dropping them from management.
            if (!restore(existing->second)) {
                existing->second.floating = true;
                existing->second.visible  = false;
                return true; // Keep the original snapshot for a later restoration retry.
            }
            m_windows.erase(existing);
            return true;
        }
        const bool isNew = existing == m_windows.end();
        if (isNew) {
            SWindowRecord record;
            record.id         = m_nextID++;
            record.hwnd       = window;
            record.process    = pid;
            record.thread     = thread;
            record.executable = executablePath(pid);
            existing          = m_windows.emplace(window, std::move(record)).first;
        }
        auto&      record         = existing->second;
        const auto monitor        = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        const bool wasMinimized   = record.minimized;
        const bool wasFloating    = record.floating;
        const bool monitorChanged = record.monitor != monitor;
        record.title              = title.data();
        record.windowClass        = name;
        record.owner              = owner;
        record.visible            = policy.visible;
        record.minimized          = policy.minimized;
        record.bounds             = windowBounds(window);
        record.monitor            = monitor;
        record.floating           = record.userFloating || record.rejected || disposition == EDisposition::Floating;
        if (!wasFloating && record.floating)
            restore(record);
        return isNew || wasMinimized != record.minimized || wasFloating != record.floating || monitorChanged;
    }

    void CWindowManager::refreshAll() {
        EnumWindows(
            [](HWND window, LPARAM data) -> BOOL {
                reinterpret_cast<CWindowManager*>(data)->refresh(window);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
        std::erase_if(m_windows, [](const auto& entry) { return !IsWindow(entry.first); });
    }

    bool CWindowManager::event(DWORD type, HWND window) {
        if (type == EVENT_OBJECT_DESTROY)
            return m_windows.erase(window) != 0;
        if (type == EVENT_SYSTEM_FOREGROUND)
            return false;
        if (type == EVENT_SYSTEM_MOVESIZESTART) {
            const auto it = m_windows.find(window);
            if (it != m_windows.end())
                it->second.interacting = true;
            return false;
        }
        if (type == EVENT_SYSTEM_MOVESIZEEND) {
            auto it = m_windows.find(window);
            if (it != m_windows.end())
                it->second.interacting = false;
            if (it != m_windows.end() && m_enabled && it->second.hasOriginal) {
                // An explicit user drag is an opt-out. Keep the user-selected geometry.
                it->second.userFloating = it->second.floating = true;
                return true;
            }
        }
        if (type != EVENT_OBJECT_CREATE && type != EVENT_OBJECT_SHOW && type != EVENT_OBJECT_HIDE && type != EVENT_OBJECT_LOCATIONCHANGE && type != EVENT_OBJECT_NAMECHANGE &&
            type != EVENT_SYSTEM_MINIMIZESTART && type != EVENT_SYSTEM_MINIMIZEEND)
            return false;
        return refresh(window);
    }

    bool CWindowManager::restore(SWindowRecord& record) {
        if (!record.hasOriginal)
            return true;
        DWORD       pid    = 0;
        const DWORD thread = GetWindowThreadProcessId(record.hwnd, &pid);
        if (pid != record.process || thread != record.thread || !IsWindow(record.hwnd)) {
            record.hasOriginal = false;
            return true;
        }
        if (IsHungAppWindow(record.hwnd)) {
            m_status = L"A hung application could not be restored; original placement retained for retry.";
            return false;
        }
        auto placement = record.original;
        if (!IsWindowVisible(record.hwnd))
            placement.showCmd = SW_HIDE;
        else if (placement.showCmd == SW_SHOWNORMAL || placement.showCmd == SW_RESTORE)
            placement.showCmd = SW_SHOWNOACTIVATE;
        if (!SetWindowPlacement(record.hwnd, &placement)) {
            m_status = L"Some windows could not be restored (permissions or application refusal).";
            return false;
        }
        record.hasOriginal = false;
        record.requested.reset();
        record.actualAfterPlacement.reset();
        return true;
    }

    void CWindowManager::restoreAll() {
        for (auto& [window, record] : m_windows)
            restore(record);
    }

    void CWindowManager::setEnabled(bool enabled) {
        if (m_enabled == enabled)
            return;
        m_enabled = enabled;
        m_status.clear();
        if (!enabled) {
            restoreAll();
            return;
        }
        refreshAll();
        relayout();
    }

    bool CWindowManager::enabled() const {
        return m_enabled;
    }
    void CWindowManager::setLayout(ELayout layout) {
        m_layout = layout;
        relayout();
    }
    ELayout CWindowManager::layout() const {
        return m_layout;
    }
    void CWindowManager::setGap(int gap) {
        m_gap = std::clamp(gap, 0, 64);
        relayout();
    }
    int CWindowManager::gap() const {
        return m_gap;
    }

    void CWindowManager::relayout() {
        if (!m_enabled)
            return;
        std::map<HMONITOR, std::vector<WindowID>>                 monitors;
        std::map<WindowID, std::reference_wrapper<SWindowRecord>> records;
        for (auto& [window, record] : m_windows) {
            if (record.floating || record.minimized || !record.visible || !IsWindow(window))
                continue;
            monitors[record.monitor].push_back(record.id);
            records.emplace(record.id, record);
        }
        for (auto& [monitor, ids] : monitors) {
            std::sort(ids.begin(), ids.end());
            MONITORINFO info{
                sizeof(MONITORINFO),
            };
            if (!GetMonitorInfoW(monitor, &info))
                continue;
            UINT dpiX = 96, dpiY = 96;
            GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            const int   gap = MulDiv(m_gap, static_cast<int>(dpiX), 96);
            const SRect work{
                info.rcWork.left,
                info.rcWork.top,
                info.rcWork.right - info.rcWork.left,
                info.rcWork.bottom - info.rcWork.top,
            };
            const auto placements = arrange(ids, work, m_layout, gap, gap);
            if (placements.empty()) {
                m_status = L"Too many windows for this work area; keeping original placements.";
                for (const auto id : ids)
                    restore(records.at(id).get());
                continue;
            }
            for (const auto& placement : placements) {
                auto& record = records.at(placement.id).get();
                if (record.interacting)
                    continue;
                const auto current = windowBounds(record.hwnd);
                // Only policy/topology changes cause placement. Location feedback never
                // retries an unchanged request, even for asynchronously constrained apps.
                if (record.requested == placement.rect || closeBounds(current, placement.rect))
                    continue;
                if (!record.hasOriginal) {
                    record.original.length = sizeof(WINDOWPLACEMENT);
                    if (!GetWindowPlacement(record.hwnd, &record.original)) {
                        record.rejected = record.floating = true;
                        m_status                          = L"Unable to read original placement; window left floating.";
                        continue;
                    }
                    record.hasOriginal = true;
                }
                if (IsZoomed(record.hwnd))
                    ShowWindow(record.hwnd, SW_SHOWNOACTIVATE);
                RECT frame{};
                GetWindowRect(record.hwnd, &frame);
                const auto  visible = windowBounds(record.hwnd);
                const int   left = visible.x - frame.left, top = visible.y - frame.top;
                const int   right = frame.right - visible.x - visible.width, bottom = frame.bottom - visible.y - visible.height;
                const auto& rect = placement.rect;
                MINMAXINFO  constraints{};
                DWORD_PTR   response = 0;
                // WM_GETMINMAXINFO is a system-marshaled message. Timeout bounds
                // cross-process constraint negotiation without input injection.
                const bool responsive = SendMessageTimeoutW(record.hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&constraints),
                                                            SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 80, &response) != 0;
                const int  width = rect.width + left + right, height = rect.height + top + bottom;
                if (!responsive || width < constraints.ptMinTrackSize.x || height < constraints.ptMinTrackSize.y ||
                    (constraints.ptMaxTrackSize.x > 0 && width > constraints.ptMaxTrackSize.x) || (constraints.ptMaxTrackSize.y > 0 && height > constraints.ptMaxTrackSize.y)) {
                    record.rejected = record.floating = true;
                    restore(record);
                    m_status = L"An application cannot fit its tile or is unresponsive; left floating.";
                    PostMessageW(m_panel, LAYOUT_MESSAGE, 0, 0);
                    continue;
                }
                record.requested            = rect;
                const BOOL moved            = SetWindowPos(record.hwnd, nullptr, rect.x - left, rect.y - top, rect.width + left + right, rect.height + top + bottom,
                                                           SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
                record.actualAfterPlacement = windowBounds(record.hwnd);
                record.bounds               = *record.actualAfterPlacement;
                if (!moved) {
                    record.rejected = record.floating = true;
                    restore(record);
                    m_status = L"An application rejected its tile or size constraints; restored and floating.";
                    PostMessageW(m_panel, LAYOUT_MESSAGE, 0, 0);
                } else
                    PostMessageW(m_panel, RECONCILE_MESSAGE, 0, 0);
            }
        }
    }

    void CWindowManager::reconcile() {
        if (!m_enabled)
            return;
        for (auto& [window, record] : m_windows) {
            if (!record.requested || record.floating || record.minimized || record.interacting || !IsWindow(window))
                continue;
            record.actualAfterPlacement = windowBounds(window);
            record.bounds               = *record.actualAfterPlacement;
            if (closeBounds(record.bounds, *record.requested))
                continue;
            record.rejected = record.floating = true;
            restore(record);
            m_status = L"An application rejected its geometry; restored and floating.";
            PostMessageW(m_panel, LAYOUT_MESSAGE, 0, 0);
        }
    }

    void CWindowManager::toggleFloating() {
        const auto it = m_windows.find(GetForegroundWindow());
        if (it == m_windows.end())
            return;
        auto& record        = it->second;
        record.userFloating = !record.floating;
        record.rejected     = false;
        record.floating     = record.userFloating || record.owner != nullptr || !(GetWindowLongPtrW(record.hwnd, GWL_STYLE) & WS_THICKFRAME);
        if (record.floating)
            restore(record);
        relayout();
    }

    void CWindowManager::focus(EDirection direction) {
        if (!m_enabled)
            return;
        const auto current = m_windows.find(GetForegroundWindow());
        if (current == m_windows.end())
            return;
        std::vector<SPlacement> candidates;
        for (const auto& [window, record] : m_windows) {
            if (!record.floating && !record.minimized)
                candidates.push_back({
                    record.id,
                    windowBounds(window),
                });
        }
        const auto target = directionalFocus(candidates, current->second.id, direction);
        if (!target)
            return;
        for (const auto& [window, record] : m_windows) {
            if (record.id == *target && !SetForegroundWindow(window))
                m_status = L"Windows declined the foreground activation request.";
        }
    }

    size_t CWindowManager::managedCount() const {
        return std::count_if(m_windows.begin(), m_windows.end(), [this](const auto& entry) {
            const auto& record = entry.second;
            return m_enabled && !record.floating && !record.minimized;
        });
    }
    size_t CWindowManager::trackedCount() const {
        return m_windows.size();
    }
    const std::wstring& CWindowManager::status() const {
        return m_status;
    }
}
