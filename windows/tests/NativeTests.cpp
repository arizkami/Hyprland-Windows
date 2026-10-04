#include "../platform/win32/IpcTransport.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <array>
#include <algorithm>
#include <charconv>
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
struct SCliResult {
    DWORD       code = 1;
    std::string output;
};
static SCliResult cli(const wchar_t* executable, const std::wstring& arguments) {
    SECURITY_ATTRIBUTES security{
        sizeof(SECURITY_ATTRIBUTES),
        nullptr,
        TRUE,
    };
    HANDLE rawRead = nullptr, rawWrite = nullptr;
    require(CreatePipe(&rawRead, &rawWrite, &security, 0), "Create CLI output pipe");
    WindowsIPC::Handle read(rawRead), write(rawWrite);
    require(SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0), "Protect parent pipe handle");
    STARTUPINFOW startup{};
    startup.cb         = sizeof(startup);
    startup.dwFlags    = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = write.get();
    startup.hStdInput                      = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring        command = L"\"" + std::wstring(executable) + L"\" " + arguments;
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process), "Launch hyprctl.exe");
    WindowsIPC::Handle child(process.hProcess), thread(process.hThread);
    write.reset();
    SCliResult result;
    const auto deadline = GetTickCount64() + 10000;
    bool       exited   = false;
    for (;;) {
        DWORD available = 0;
        while (PeekNamedPipe(read.get(), nullptr, 0, nullptr, &available, nullptr) && available) {
            std::array<char, 4096> buffer{};
            DWORD                  bytes = 0;
            require(ReadFile(read.get(), buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &bytes, nullptr), "Read CLI output");
            result.output.append(buffer.data(), bytes);
        }
        if (exited)
            break;
        exited = WaitForSingleObject(child.get(), 0) == WAIT_OBJECT_0;
        if (GetTickCount64() >= deadline) {
            TerminateProcess(child.get(), 2);
            throw std::runtime_error("CLI deadline exceeded");
        }
        pump(10);
    }
    require(GetExitCodeProcess(child.get(), &result.code), "CLI exit code");
    result.output.erase(std::remove(result.output.begin(), result.output.end(), '\r'), result.output.end());
    return result;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3)
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
        auto commandResult = cli(argv[2], L"-j clients");
        require(commandResult.code == 0 && commandResult.output.find("Native test: first") != std::string::npos && commandResult.output.find("excluded tool") == std::string::npos,
                "hyprctl clients must enumerate scoped tracked windows");
        require(cli(argv[2], L"-j monitors").output.find("workArea") != std::string::npos, "hyprctl monitor work area");
        require(cli(argv[2], L"-j instances").output.find(std::to_string(process.dwProcessId)) != std::string::npos, "hyprctl running instance PID");
        require(cli(argv[2], L"dispatch exec calc.exe").code == 1, "Unsupported dispatcher must fail");
        require(cli(argv[2], L"keyword general:gaps_in 65").code == 1, "Out-of-range keyword must fail");
        require(cli(argv[2], L"keyword general:gaps_in 7junk").code == 1, "Malformed keyword must fail");
        require(cli(argv[2], L"--batch \"keyword general:gaps_in 7; keyword general:gaps_out 19\"").code == 0, "CLI batch independent gaps");
        require(cli(argv[2], L"-j getoption general:gaps_in").output == "{\"option\": \"general:gaps_in\", \"custom\": \"7 7 7 7\", \"set\": true }\n", "Inner gap persisted");
        require(cli(argv[2], L"getoption general:gaps_out").output == "custom type: 19 19 19 19\nset: true\n", "Outer gap independent");
        require(cli(argv[2], L"getoption general:layout").output == "str: dwindle\nset: false\n", "Unset option reports default");
        require(cli(argv[2], L"--batch \"keyword general:gaps_in 12; invalid; keyword general:gaps_out 3\"").code == 1, "Batch stops on failure");
        require(cli(argv[2], L"getoption general:gaps_out").output.starts_with("custom type: 19 "), "Batch does not run commands after failure");
        require(cli(argv[2], L"-q keyword general:gaps_out 12").output.empty(), "Quiet suppresses success output");
        require(cli(argv[2], L"getoption general:nonexistent").code == 1, "Unknown option must fail");
        require(cli(argv[2], L"dispatch togglefloating id:999999").code == 1, "Unknown client ID must fail");
        require(cli(argv[2], L"dispatch focuswindow title:(").code == 1, "Invalid selector regex must fail");
        require(cli(argv[2], L"dispatch movefocus x").code == 1, "Invalid direction must fail");
        require(cli(argv[2], L"dispatch exit now").code == 1, "Argument-free dispatcher rejects arguments");
        require(cli(argv[2], L"-i 1 version").code == 1, "Only instance 0 exists on Windows");
        require(cli(argv[2], L"").code == 1, "Missing command prints usage and fails");
        const auto address = [](HWND window) {
            std::array<char, 32> text{};
            const auto           end = std::to_chars(text.data(), text.data() + text.size(), reinterpret_cast<uintptr_t>(window), 16).ptr;
            return std::string("0x") + std::string(text.data(), end);
        };
        const auto wideText = [](const std::string& text) { return std::wstring(text.begin(), text.end()); };
        require(commandResult.output.find("\"address\": \"" + address(first) + '"') != std::string::npos, "JSON clients report Hyprland window addresses");
        const auto clientsText = cli(argv[2], L"clients").output;
        require(clientsText.find("Window " + address(first).substr(2) + " -> Native test: first:\n") != std::string::npos &&
                    clientsText.find("\tclass: HyprlandNativeTestFixture\n") != std::string::npos,
                "Plain clients output follows Hyprland's text format");
        const auto version = cli(argv[2], L"-j version").output;
        require(version.find("\"platform\": \"windows\"") != std::string::npos && version.find("\"commit\": ") != std::string::npos, "JSON version");
        require(cli(argv[2], L"version").output.starts_with("Hyprland "), "Plain version");
        const auto binds = cli(argv[2], L"-j binds").output;
        require(binds.find("\"modmask\": 12, \"submap\": \"\", \"key\": \"T\"") != std::string::npos && binds.find("\"dispatcher\": \"togglemanagement\"") != std::string::npos,
                "Binds report Ctrl+Alt dispatchers");
        require(cli(argv[2], L"cursorpos").output.find(", ") != std::string::npos, "Cursor position");
        require(cli(argv[2], L"-j layouts").output == "[\"dwindle\", \"master\"]\n", "Layouts");
        require(cli(argv[2], L"monitors").output.starts_with("Monitor "), "Plain monitors");
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
        AllowSetForegroundWindow(process.dwProcessId);
        require(cli(argv[2], L"dispatch focuswindow address:" + wideText(address(second))).code == 0 && GetForegroundWindow() == second, "focuswindow by address");
        require(cli(argv[2], L"-j activewindow").output.find("Native test: second") != std::string::npos, "activewindow follows focus");
        AllowSetForegroundWindow(process.dwProcessId);
        require(cli(argv[2], L"dispatch focuswindow title:^Native test: first$").code == 0 && GetForegroundWindow() == first, "focuswindow by title regex");
        require(cli(argv[2], L"dispatch setfloating address:" + wideText(address(second))).code == 0 && query(4) == 1, "setfloating");
        require(cli(argv[2], L"dispatch setfloating address:" + wideText(address(second))).code == 0 && query(4) == 1, "setfloating is idempotent");
        require(cli(argv[2], L"dispatch settiled address:" + wideText(address(second))).code == 0 && query(4) == 2, "settiled");
        const HWND closeTarget = create(L"Native test: close target", nullptr);
        pump(500);
        require(query(4) == 3, "Close target managed");
        require(cli(argv[2], L"dispatch closewindow title:close target$").code == 0, "closewindow by title");
        pump(500);
        require(!IsWindow(closeTarget) && query(4) == 2, "closewindow asks the application to close");
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
        require(cli(argv[2], L"dispatch disable").code == 0, "CLI disable restores");
        require(same(bounds(first), firstOriginal) && same(bounds(second), secondOriginal), "CLI restoration");
        require(cli(argv[2], L"dispatch enable").code == 0, "CLI enable tiles");
        require(cli(argv[2], L"dispatch exit").code == 0, "CLI exit reply arrives before shutdown");
        pump(400);
        require(WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0, "CLI exit closes manager");
        require(same(bounds(first), firstOriginal) && same(bounds(second), secondOriginal), "Normal exit restores originals");
        require(RegisterHotKey(nullptr, 99, MOD_CONTROL | MOD_ALT, 'T'), "Hotkeys released on exit");
        UnregisterHotKey(nullptr, 99);
        std::cout << "Native rendering/resize, hyprctl IPC, opt-in, hotkeys/focus, policy, lifecycle, constraints and restoration passed\n";
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
