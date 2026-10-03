#include "Graphics.hpp"
#include "Panel.hpp"
#include "WindowManager.hpp"
#include <shellapi.h>
#include <windowsx.h>
#include <array>
#include <memory>
#include <stdexcept>

using namespace WindowsUI;
using namespace WindowsTiling;

class CApplication {
  public:
    explicit CApplication(DWORD testProcess);
    ~CApplication();
    int     run();
    LRESULT message(UINT type, WPARAM wparam, LPARAM lparam);

  private:
    HWND                                m_window      = nullptr;
    DWORD                               m_testProcess = 0;
    std::unique_ptr<CGraphicEngineCore> m_graphics;
    std::unique_ptr<CWindowManager>     m_manager;
    CPanel                              m_panel;
    std::wstring                        m_hotkeyStatus;
    std::vector<int>                    m_hotkeys;
    bool                                m_relayoutPending  = false;
    bool                                m_reconcilePending = false;
    EAction                             m_pressed          = EAction::None;
    void                                update();
    void                                action(EAction action);
    void                                registerHotkeys();
    static LRESULT CALLBACK             procedure(HWND window, UINT type, WPARAM wparam, LPARAM lparam);
};

CApplication::CApplication(DWORD testProcess) : m_testProcess(testProcess) {
    WNDCLASSEXW windowClass{
        sizeof(WNDCLASSEXW),
    };
    windowClass.hInstance     = GetModuleHandleW(nullptr);
    windowClass.lpfnWndProc   = procedure;
    windowClass.lpszClassName = L"HyprlandWindowsPanel";
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&windowClass))
        throw std::runtime_error("Unable to register panel window class");
    m_window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, windowClass.lpszClassName, L"Hyprland Windows · Native milestone", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 540,
                               650, nullptr, nullptr, windowClass.hInstance, this);
    if (!m_window)
        throw std::runtime_error("Unable to create panel");
    try {
        m_graphics = std::make_unique<CGraphicEngineCore>(m_window);
        m_manager  = std::make_unique<CWindowManager>(m_window, testProcess);
        registerHotkeys();
        // CreateWindowEx sizes are physical pixels under PMv2. Size the initial
        // panel in DIP after Windows has selected its actual monitor and DPI.
        const auto dpi = static_cast<int>(GetDpiForWindow(m_window));
        SetWindowPos(m_window, nullptr, 0, 0, MulDiv(540, dpi, 96), MulDiv(650, dpi, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT client{};
        GetClientRect(m_window, &client);
        m_graphics->resize(client.right, client.bottom, GetDpiForWindow(m_window));
        update();
        ShowWindow(m_window, SW_SHOWNORMAL);
    } catch (...) {
        m_manager.reset();
        m_graphics.reset();
        DestroyWindow(m_window);
        throw;
    }
}

CApplication::~CApplication() {
    for (int id : m_hotkeys)
        UnregisterHotKey(m_window, id);
    m_manager.reset();
    m_graphics.reset();
    if (IsWindow(m_window))
        DestroyWindow(m_window);
}

void CApplication::registerHotkeys() {
    const std::array<UINT, 8> keys{{
        'T',
        'L',
        'F',
        'Q',
        VK_LEFT,
        VK_RIGHT,
        VK_UP,
        VK_DOWN,
    }};
    for (size_t i = 0; i < keys.size(); ++i) {
        const int id = static_cast<int>(i) + 1;
        if (RegisterHotKey(m_window, id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, keys[i]))
            m_hotkeys.push_back(id);
        else {
            if (m_hotkeyStatus.empty())
                m_hotkeyStatus = L"Unavailable Ctrl+Alt keys: ";
            const std::array<std::wstring, 4> arrows{{
                L"Left",
                L"Right",
                L"Up",
                L"Down",
            }};
            m_hotkeyStatus += (i < 4 ? std::wstring(1, static_cast<wchar_t>(keys[i])) : arrows[i - 4]) + L" ";
        }
    }
    if (!m_hotkeyStatus.empty())
        m_hotkeyStatus += L"\nUse panel controls.";
}

void CApplication::update() {
    if (!m_manager || !m_graphics)
        return;
    RECT rect{};
    GetClientRect(m_window, &rect);
    const float scale = GetDpiForWindow(m_window) / 96.0F;
    m_panel.layout(rect.right / scale, rect.bottom / scale,
                   {
                       m_manager->enabled(),
                       m_manager->layout() == ELayout::Master,
                       m_manager->gap(),
                       m_manager->trackedCount(),
                       m_manager->managedCount(),
                       m_manager->status(),
                       m_hotkeyStatus,
                   });
    InvalidateRect(m_window, nullptr, FALSE);
}

void CApplication::action(EAction command) {
    switch (command) {
        case EAction::Toggle: m_manager->setEnabled(!m_manager->enabled()); break;
        case EAction::Layout: m_manager->setLayout(m_manager->layout() == ELayout::Master ? ELayout::Dwindle : ELayout::Master); break;
        case EAction::GapDown: m_manager->setGap(m_manager->gap() - 2); break;
        case EAction::GapUp: m_manager->setGap(m_manager->gap() + 2); break;
        case EAction::Exit: DestroyWindow(m_window); return;
        default: return;
    }
    update();
}

LRESULT CApplication::message(UINT type, WPARAM wparam, LPARAM lparam) {
    if (type == WM_APP + 10 && m_testProcess) {
        // Scoped native integration harness only. Production has no IPC control surface.
        if (wparam == 1)
            action(EAction::Toggle);
        else if (wparam == 2)
            action(EAction::Layout);
        else if (wparam == 3)
            return static_cast<LRESULT>(m_graphics->frames());
        else if (wparam == 4)
            return static_cast<LRESULT>(m_manager->managedCount());
        else if (wparam == 5)
            m_manager->toggleFloating();
        return 0;
    }
    switch (type) {
        case RECONCILE_MESSAGE:
            if (!m_reconcilePending) {
                m_reconcilePending = true;
                SetTimer(m_window, 2, 140, nullptr);
            }
            return 0;
        case LAYOUT_MESSAGE:
            if (!m_relayoutPending) {
                m_relayoutPending = true;
                SetTimer(m_window, 1, 70, nullptr);
            }
            return 0;
        case TRACKER_MESSAGE:
            if (m_manager && m_manager->event(static_cast<DWORD>(wparam), reinterpret_cast<HWND>(lparam))) {
                // Coalesce bursts without continuously polling the desktop.
                if (!m_relayoutPending) {
                    m_relayoutPending = true;
                    SetTimer(m_window, 1, 70, nullptr);
                }
            }
            return 0;
        case WM_TIMER:
            if (wparam == 2) {
                KillTimer(m_window, 2);
                m_reconcilePending = false;
                m_manager->reconcile();
                update();
            }
            if (wparam == 1) {
                KillTimer(m_window, 1);
                m_relayoutPending = false;
                m_manager->relayout();
                update();
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (m_manager) {
                m_manager->refreshAll();
                m_manager->relayout();
                update();
            }
            return 0;
        case WM_SIZE:
            if (m_graphics && wparam != SIZE_MINIMIZED) {
                RECT rect{};
                GetClientRect(m_window, &rect);
                m_graphics->resize(rect.right, rect.bottom, GetDpiForWindow(m_window));
                update();
            }
            return 0;
        case WM_DPICHANGED: {
            const auto rect = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(m_window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            RECT client{};
            GetClientRect(m_window, &client);
            if (m_graphics)
                m_graphics->resize(client.right, client.bottom, HIWORD(wparam));
            update();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto       limits      = reinterpret_cast<MINMAXINFO*>(lparam);
            const UINT dpi         = GetDpiForWindow(m_window);
            limits->ptMinTrackSize = {
                MulDiv(400, static_cast<int>(dpi ? dpi : 96), 96),
                MulDiv(640, static_cast<int>(dpi ? dpi : 96), 96),
            };
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const float scale = GetDpiForWindow(m_window) / 96.0F;
            m_pressed         = m_panel.hit(GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale);
            SetCapture(m_window);
            return 0;
        }
        case WM_LBUTTONUP: {
            const float   scale   = GetDpiForWindow(m_window) / 96.0F;
            const EAction hit     = m_panel.hit(GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale);
            const EAction pressed = m_pressed;
            m_pressed             = EAction::None;
            ReleaseCapture();
            if (hit == pressed)
                action(hit);
            return 0;
        }
        case WM_CAPTURECHANGED: m_pressed = EAction::None; return 0;
        case WM_KEYDOWN:
            if (wparam == VK_TAB) {
                m_panel.nextFocus();
                update();
            } else if (wparam == VK_RETURN || wparam == VK_SPACE)
                action(m_panel.focusedAction());
            return 0;
        case WM_HOTKEY:
            if (wparam == 1)
                action(EAction::Toggle);
            else if (wparam == 2)
                action(EAction::Layout);
            else if (wparam == 3) {
                m_manager->toggleFloating();
                update();
            } else if (wparam == 4)
                action(EAction::Exit);
            else if (wparam >= 5 && wparam <= 8) {
                m_manager->focus(static_cast<EDirection>(wparam - 5));
                update();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(m_window, &paint);
            EndPaint(m_window, &paint);
            if (m_graphics && !IsIconic(m_window) && !m_graphics->draw(m_panel.commands()))
                InvalidateRect(m_window, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(m_window); return 0;
        case WM_DESTROY:
            for (int id : m_hotkeys)
                UnregisterHotKey(m_window, id);
            m_hotkeys.clear();
            m_manager.reset();
            PostQuitMessage(0);
            return 0;
        default: return DefWindowProcW(m_window, type, wparam, lparam);
    }
}

LRESULT CALLBACK CApplication::procedure(HWND window, UINT type, WPARAM wparam, LPARAM lparam) {
    auto app = reinterpret_cast<CApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (type == WM_NCCREATE) {
        const auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app               = static_cast<CApplication*>(create->lpCreateParams);
        app->m_window     = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, type, wparam, lparam);
    // Exceptions must never unwind through a Win32 callback.
    try {
        return app->message(type, wparam, lparam);
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "Hyprland Windows failure (restoring windows)", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        return 0;
    }
}

int CApplication::run() {
    MSG  message{};
    BOOL result = FALSE;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        const auto                                   closeHandle = [](void* handle) { CloseHandle(handle); };
        std::unique_ptr<void, decltype(closeHandle)> instance(CreateMutexW(nullptr, FALSE, L"Local\\HyprlandWindowsNative"), closeHandle);
        if (!instance)
            throw std::runtime_error("Unable to create application instance guard");
        if (GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Hyprland Windows is already running in this session");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int   count       = 0;
        auto  args        = CommandLineToArgvW(GetCommandLineW(), &count);
        DWORD testProcess = 0;
        if (count == 3 && std::wstring(args[1]) == L"--test-process") {
            try {
                testProcess = std::stoul(args[2]);
            } catch (...) {
                LocalFree(args);
                throw;
            }
            if (!testProcess) {
                LocalFree(args);
                throw std::runtime_error("--test-process requires a nonzero process ID");
            }
        } else if (count != 1) {
            LocalFree(args);
            throw std::runtime_error("Usage: HyprlandWindows.exe [--test-process PID]");
        }
        LocalFree(args);
        CApplication application(testProcess);
        return application.run();
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Hyprland Windows", MB_OK | MB_ICONERROR);
        return 1;
    }
}
