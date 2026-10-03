#include <windows.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
static void pump(int milliseconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    } while (std::chrono::steady_clock::now() < end);
}
static LRESULT CALLBACK fixture(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_WINDOWPOSCHANGING && GetWindowLongPtrW(window, GWLP_USERDATA) >= 1) {
        auto position = reinterpret_cast<WINDOWPOS*>(lparam);
        if (!(position->flags & SWP_NOSIZE) && (position->cx != 480 || position->cy != 350)) {
            position->cx = std::max(position->cx, 1800);
            position->cy = std::max(position->cy, 950);
        }
        return 0;
    }
    if (message == WM_GETMINMAXINFO && GetWindowLongPtrW(window, GWLP_USERDATA) == 1) {
        auto limits            = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize = {
            1800,
            950,
        };
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
static RECT bounds(HWND window) {
    RECT r{};
    GetWindowRect(window, &r);
    return r;
}
static bool same(RECT a, RECT b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
static void hotkey(WORD key) {
    std::array<INPUT, 6>      events{};
    const std::array<WORD, 6> keys{{
        VK_CONTROL,
        VK_MENU,
        key,
        key,
        VK_MENU,
        VK_CONTROL,
    }};
    for (size_t i = 0; i < events.size(); ++i) {
        events[i].type       = INPUT_KEYBOARD;
        events[i].ki.wVk     = keys[i];
        events[i].ki.dwFlags = i >= 3 ? KEYEVENTF_KEYUP : 0;
    }
    require(SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT)) == events.size(), "Hotkey SendInput failed");
    pump(450);
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpfnWndProc   = fixture;
    wc.lpszClassName = L"HyprlandNativeTestFixture";
    RegisterClassW(&wc);
    std::vector<HWND>   windows;
    PROCESS_INFORMATION process{};
    HWND                panel  = nullptr;
    int                 result = 1;
    try {
        require(FindWindowW(L"HyprlandWindowsPanel", nullptr) == nullptr, "Close other Hyprland Windows instances before running native hotkey tests");
        const auto create = [&](const wchar_t* title, HWND owner, DWORD exStyle = 0) {
            HWND w = CreateWindowExW(exStyle, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 100 + static_cast<int>(windows.size()) * 80,
                                     100 + static_cast<int>(windows.size()) * 50, 480, 350, owner, nullptr, wc.hInstance, nullptr);
            require(w != nullptr, "Create fixture window");
            windows.push_back(w);
            ShowWindow(w, SW_SHOWNOACTIVATE);
            return w;
        };
        const HWND   first         = create(L"Native test: first", nullptr);
        const HWND   second        = create(L"Native test: second", nullptr);
        const HWND   dialog        = create(L"Native test: owned dialog", first);
        const HWND   tool          = create(L"Native test: excluded tool", nullptr, WS_EX_TOOLWINDOW);
        const RECT   firstOriginal = bounds(first), secondOriginal = bounds(second), dialogOriginal = bounds(dialog), toolOriginal = bounds(tool);
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" --test-process " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{
            sizeof(STARTUPINFOW),
        };
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process), "Launch scoped manager");
        AllowSetForegroundWindow(process.dwProcessId);
        for (int i = 0; i < 100 && !panel; ++i) {
            panel              = FindWindowW(L"HyprlandWindowsPanel", nullptr);
            DWORD panelProcess = 0;
            if (panel)
                GetWindowThreadProcessId(panel, &panelProcess);
            if (panelProcess != process.dwProcessId)
                panel = nullptr;
            pump(50);
        }
        require(panel != nullptr, "Panel initialization timed out");
        pump(400);
        const auto query = [&](WPARAM op) { return SendMessageW(panel, WM_APP + 10, op, 0); };
        RECT       client{};
        GetClientRect(panel, &client);
        require(MulDiv(client.bottom, 96, static_cast<int>(GetDpiForWindow(panel))) >= 558, "Initial panel must fit all controls at native DPI");
        std::vector<HMONITOR> monitors;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));
        for (const auto monitor : monitors) {
            MONITORINFO info{
                sizeof(MONITORINFO),
            };
            GetMonitorInfoW(monitor, &info);
            UINT dpiX = 96, dpiY = 96;
            GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            SetWindowPos(panel, nullptr, info.rcWork.left + 30, info.rcWork.top + 30, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
            pump(250);
            require(GetDpiForWindow(panel) == dpiX, "Monitor DPI transition");
            std::cout << "Monitor DPI " << dpiX << "; panel resize/transition passed\n";
        }
        pump(250);
        require(same(bounds(first), firstOriginal) && query(4) == 0, "Startup must not tile");
        const auto frames = query(3);
        FILETIME   creation{}, exit{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
        GetProcessTimes(process.hProcess, &creation, &exit, &kernelBefore, &userBefore);
        pump(1100);
        GetProcessTimes(process.hProcess, &creation, &exit, &kernelAfter, &userAfter);
        require(query(3) == frames, "Idle must not present frames");
        const auto ticks     = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        const auto idleTicks = ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) - ticks(userBefore);
        require(idleTicks < 2000000, "Idle CPU exceeds 200 ms per 1.1 seconds");
        std::cout << "Idle frames unchanged; CPU time " << idleTicks / 10000 << " ms / 1100 ms\n";
        hotkey('T');
        require(query(4) == 2 && !same(bounds(first), firstOriginal), "Registered global hotkey enables tiling");
        require(same(bounds(dialog), dialogOriginal) && same(bounds(tool), toolOriginal), "Owned dialogs and tools stay untouched");
        RECT masterBefore = bounds(second);
        hotkey('L');
        require(!same(bounds(second), masterBefore), "Master layout hotkey changes geometry");
        // Input goes to the real shader-drawn control, including release hit testing.
        const UINT dpi = GetDpiForWindow(panel);
        const int  x = MulDiv(40, static_cast<int>(dpi), 96), y = MulDiv(18 + 74 + 10 + 20, static_cast<int>(dpi), 96);
        SendMessageW(panel, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(panel, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        pump(300);
        require(query(4) == 0 && same(bounds(first), firstOriginal) && same(bounds(second), secondOriginal), "Panel disable restores originals");
        query(1);
        pump(250);
        SetForegroundWindow(second);
        pump(100);
        require(GetForegroundWindow() == second, "Test foreground grant unavailable");
        hotkey('F');
        require(query(4) == 1 && same(bounds(second), secondOriginal), "Floating hotkey restores original placement");
        hotkey('F');
        require(query(4) == 2, "Floating hotkey returns window to tiling");
        hotkey(bounds(first).left < bounds(second).left ? VK_LEFT : VK_RIGHT);
        require(GetForegroundWindow() == first, "Directional focus hotkey");
        const HWND third = create(L"Native test: lifecycle", nullptr);
        pump(500);
        require(query(4) == 3, "New window auto-managed");
        ShowWindow(third, SW_MINIMIZE);
        pump(400);
        require(query(4) == 2, "Minimized window removed from active layout");
        ShowWindow(third, SW_SHOWNOACTIVATE);
        pump(400);
        require(query(4) == 3, "Restored window returns to layout");
        DestroyWindow(third);
        pump(400);
        require(query(4) == 2, "Destroy event relayout");
        ShowWindow(first, SW_HIDE);
        pump(400);
        require(query(4) == 1 && !IsWindowVisible(first), "Hidden survivor stays hidden during restoration");
        ShowWindow(first, SW_SHOWNOACTIVATE);
        pump(400);
        require(query(4) == 2, "Visible survivor rediscovered");
        // A constrained application refuses the tile; manager restores and floats it.
        const HWND constrained = create(L"Native test: min-size constraint", nullptr);
        SetWindowLongPtrW(constrained, GWLP_USERDATA, 1);
        const RECT constrainedOriginal = bounds(constrained);
        pump(500);
        SetWindowLongPtrW(constrained, GWLP_USERDATA, 0);
        require(query(4) == 2 && same(bounds(constrained), constrainedOriginal), "Rejected size restored and floated");
        DestroyWindow(constrained);
        const HWND rejecting = create(L"Native test: rejects geometry", nullptr);
        SetWindowLongPtrW(rejecting, GWLP_USERDATA, 2);
        const RECT rejectingOriginal = bounds(rejecting);
        pump(600);
        require(query(4) == 2 && same(bounds(rejecting), rejectingOriginal), "Delayed reconciliation restores rejected geometry");
        DestroyWindow(rejecting);
        const HWND maximized = create(L"Native test: maximized original", nullptr);
        ShowWindow(maximized, SW_MAXIMIZE);
        pump(600);
        require(query(4) == 3 && !IsZoomed(maximized), "Maximized window tiled normally");
        query(1);
        pump(400);
        require(IsZoomed(maximized), "Disable restores original maximized state");
        DestroyWindow(maximized);
        query(1);
        pump(400);
        const auto beforeResize = query(3);
        SetWindowPos(panel, nullptr, 0, 0, 700, 760, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        pump(400);
        require(query(3) > beforeResize, "Resize must redraw/present");
        hotkey('Q');
        pump(400);
        require(WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0, "Exit hotkey closes manager");
        require(same(bounds(first), firstOriginal) && same(bounds(second), secondOriginal), "Normal exit restores originals");
        require(RegisterHotKey(nullptr, 99, MOD_CONTROL | MOD_ALT, 'T'), "Hotkeys released on exit");
        UnregisterHotKey(nullptr, 99);
        std::cout << "Native rendering/resize, opt-in, hotkeys/focus, policy, lifecycle, constraints and restoration passed\n";
        result = 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    if (panel && IsWindow(panel)) {
        PostMessageW(panel, WM_CLOSE, 0, 0);
        pump(800);
    }
    if (process.hProcess) {
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    }
    for (auto window : windows) {
        if (IsWindow(window))
            DestroyWindow(window);
    }
    return result;
}
