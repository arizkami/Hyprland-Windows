#include "Commands.hpp"
#include "Bindings.hpp"
#include "WindowManager.hpp"
// Shared upstream header; its new-year splash uses localtime (MSVC C4996).
#pragma warning(push)
#pragma warning(disable : 4996)
#include "../../../src/helpers/Splashes.hpp"
#pragma warning(pop)
#include <WindowsVersion.hpp>
#include <shellscalingapi.h>
#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <random>
#include <regex>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace WindowsTiling;
using namespace WindowsIPC;

static std::string jsonString(std::string_view bytes) {
    constexpr char hex[]  = "0123456789abcdef";
    std::string    result = "\"";
    for (const unsigned char c : bytes) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else
            result += static_cast<char>(c);
    }
    return result + '"';
}

static std::string jsonString(const std::wstring& text) {
    return jsonString(utf8(text));
}

static const char* jsonBool(bool value) {
    return value ? "true" : "false";
}

static std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// Splits the first whitespace-delimited token from the trimmed remainder.
static std::pair<std::string_view, std::string_view> split(std::string_view text) {
    text           = trim(text);
    const auto end = text.find_first_of(" \t");
    if (end == std::string_view::npos)
        return {text, {}};
    return {text.substr(0, end), trim(text.substr(end))};
}

template <typename T>
static std::optional<T> number(std::string_view text, int base = 10) {
    T          value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

struct SMonitor {
    HMONITOR       handle = nullptr;
    MONITORINFOEXW info{};
};

struct SMonitorList {
    std::vector<SMonitor> entries;
    bool                  failed = false;
};

static BOOL CALLBACK collectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM parameter) {
    auto&    list = *reinterpret_cast<SMonitorList*>(parameter);
    SMonitor monitor{handle};
    monitor.info.cbSize = sizeof(monitor.info);
    if (!GetMonitorInfoW(handle, &monitor.info)) {
        list.failed = true;
        return FALSE;
    }
    // Never unwind through the Win32 enumeration callback.
    try {
        list.entries.push_back(monitor);
        return TRUE;
    } catch (...) {
        list.failed = true;
        return FALSE;
    }
}

static std::vector<SMonitor> monitors() {
    SMonitorList list;
    if (!EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&list)) || list.failed)
        throw std::runtime_error("Monitor enumeration failed");
    return std::move(list.entries);
}

static int monitorID(const std::vector<SMonitor>& list, HMONITOR handle) {
    const auto it = std::ranges::find(list, handle, &SMonitor::handle);
    return it == list.end() ? -1 : static_cast<int>(it - list.begin());
}

static std::string monitorData(const SMonitor& monitor, int id, bool json) {
    const auto&  info = monitor.info;
    std::wstring name(info.szDevice);
    if (name.starts_with(L"\\\\.\\"))
        name.erase(0, 4);
    DISPLAY_DEVICEW device{sizeof(DISPLAY_DEVICEW)};
    const auto      description = EnumDisplayDevicesW(info.szDevice, 0, &device, 0) ? utf8(device.DeviceString) : std::string{};
    DEVMODEW        mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        mode = {};
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor.handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
        dpiX = 96;
    const double scale     = dpiX / 96.0;
    const int    transform = (mode.dmFields & DM_DISPLAYORIENTATION) ? static_cast<int>(mode.dmDisplayOrientation) : 0;
    const bool   focused   = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY) == monitor.handle;
    const auto&  bounds    = info.rcMonitor;
    const auto&  work      = info.rcWork;
    const int    width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    const int    reservedLeft = work.left - bounds.left, reservedTop = work.top - bounds.top;
    const int    reservedRight = bounds.right - work.right, reservedBottom = bounds.bottom - work.bottom;
    const double refresh = mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 0.0;
    if (json)
        return std::format(R"({{"id": {}, "name": {}, "description": {}, "width": {}, "height": {}, "refreshRate": {:.5f}, "x": {}, "y": {}, )"
                           R"("reserved": [{}, {}, {}, {}], "scale": {}, "transform": {}, "focused": {}, "disabled": false, "primary": {}, "workArea": [{}, {}, {}, {}]}})",
                           id, jsonString(name), jsonString(description), width, height, refresh, bounds.left, bounds.top, reservedLeft, reservedTop, reservedRight, reservedBottom,
                           scale, transform, jsonBool(focused), jsonBool(info.dwFlags & MONITORINFOF_PRIMARY), work.left, work.top, work.right - work.left, work.bottom - work.top);
    return std::format("Monitor {} (ID {}):\n\t{}x{}@{:.5f} at {}x{}\n\tdescription: {}\n\treserved: {} {} {} {}\n\tscale: {}\n\ttransform: {}\n\tfocused: {}\n\t"
                       "disabled: false\n\tprimary: {}\n\tworkArea: {} {} {} {}\n\n",
                       utf8(name), id, width, height, refresh, bounds.left, bounds.top, description, reservedLeft, reservedTop, reservedRight, reservedBottom, scale, transform,
                       focused ? "yes" : "no", (info.dwFlags & MONITORINFOF_PRIMARY) ? "yes" : "no", work.left, work.top, work.right - work.left, work.bottom - work.top);
}

static std::string windowData(const SWindowRecord& record, int monitor, bool json) {
    const auto address = reinterpret_cast<uintptr_t>(record.hwnd);
    const bool visible = record.visible && !record.minimized;
    if (json)
        return std::format(R"({{"address": "0x{:x}", "mapped": true, "hidden": {}, "visible": {}, "at": [{}, {}], "size": [{}, {}], "floating": {}, "monitor": {}, )"
                           R"("class": {}, "title": {}, "pid": {}, "xwayland": false, "pinned": false, "fullscreen": 0, "id": {}, "executable": {}, "minimized": {}}})",
                           address, jsonBool(record.minimized), jsonBool(visible), record.bounds.x, record.bounds.y, record.bounds.width, record.bounds.height,
                           jsonBool(record.floating), monitor, jsonString(record.windowClass), jsonString(record.title), record.process, record.id, jsonString(record.executable),
                           jsonBool(record.minimized));
    return std::format("Window {:x} -> {}:\n\tmapped: 1\n\thidden: {}\n\tvisible: {}\n\tat: {},{}\n\tsize: {},{}\n\tfloating: {}\n\tmonitor: {}\n\tclass: {}\n\ttitle: {}\n\t"
                       "pid: {}\n\txwayland: 0\n\tpinned: 0\n\tfullscreen: 0\n\tid: {}\n\texecutable: {}\n\n",
                       address, utf8(record.title), static_cast<int>(record.minimized), static_cast<int>(visible), record.bounds.x, record.bounds.y, record.bounds.width,
                       record.bounds.height, static_cast<int>(record.floating), monitor, utf8(record.windowClass), utf8(record.title), record.process, record.id,
                       utf8(record.executable));
}

// Hyprland window selectors: activewindow, address:0x…, pid:N, class:REGEX, title:REGEX
// or a bare class regex. id:N addresses the Windows-specific stable client ID.
static std::optional<SWindowRecord> findWindow(const std::vector<SWindowRecord>& windows, std::string_view selector) {
    const auto match = [&windows](const auto& predicate) -> std::optional<SWindowRecord> {
        const auto it = std::ranges::find_if(windows, predicate);
        if (it == windows.end())
            return std::nullopt;
        return *it;
    };
    if (selector.empty() || selector == "activewindow") {
        const HWND foreground = GetForegroundWindow();
        return match([foreground](const SWindowRecord& record) { return record.hwnd == foreground; });
    }
    if (selector.starts_with("address:")) {
        auto hex = selector.substr(8);
        if (hex.starts_with("0x") || hex.starts_with("0X"))
            hex.remove_prefix(2);
        const auto address = number<uintptr_t>(hex, 16);
        if (!address)
            throw std::invalid_argument("Invalid window address");
        return match([address](const SWindowRecord& record) { return reinterpret_cast<uintptr_t>(record.hwnd) == *address; });
    }
    if (selector.starts_with("pid:")) {
        const auto pid = number<DWORD>(selector.substr(4));
        if (!pid)
            throw std::invalid_argument("Invalid process ID");
        return match([pid](const SWindowRecord& record) { return record.process == *pid; });
    }
    if (selector.starts_with("id:")) {
        const auto id = number<WindowID>(selector.substr(3));
        if (!id || !*id)
            throw std::invalid_argument("Invalid client ID");
        return match([id](const SWindowRecord& record) { return record.id == *id; });
    }
    const bool title = selector.starts_with("title:");
    if (title || selector.starts_with("class:"))
        selector.remove_prefix(6);
    std::regex pattern;
    try {
        pattern = std::regex(std::string(selector));
    } catch (const std::regex_error&) { throw std::invalid_argument("Invalid window regex"); }
    return match([&pattern, title](const SWindowRecord& record) { return std::regex_search(utf8(title ? record.title : record.windowClass), pattern); });
}

static std::string versionData(bool json) {
#ifdef NDEBUG
    constexpr std::string_view flags = "", flagsText = "no flags were set\n";
#else
    constexpr std::string_view flags = R"("debug")", flagsText = "flags set:\ndebug\n";
#endif
    if (json)
        return std::format(R"({{"branch": {}, "commit": {}, "version": {}, "dirty": {}, "commit_message": {}, "commit_date": {}, "tag": {}, "commits": {}, )"
                           R"("platform": "windows", "flags": [{}]}})",
                           jsonString(GIT_BRANCH), jsonString(GIT_COMMIT_HASH), jsonString(HYPRLAND_VERSION), jsonBool(std::string_view(GIT_DIRTY) == "dirty"),
                           jsonString(GIT_COMMIT_MESSAGE), jsonString(GIT_COMMIT_DATE), jsonString(GIT_TAG), jsonString(GIT_COMMITS), flags);
    return std::format("Hyprland {} built from branch {} at commit {} {} ({}).\nDate: {}\nTag: {}, commits: {}\n\nPlatform: Windows native (D3D11/DirectComposition)\n{}",
                       HYPRLAND_VERSION, GIT_BRANCH, GIT_COMMIT_HASH, GIT_DIRTY, GIT_COMMIT_MESSAGE, GIT_COMMIT_DATE, GIT_TAG, GIT_COMMITS, flagsText);
}

static std::string osVersion() {
    // GetVersionEx reports the manifest-compatible version; RtlGetVersion reports the real one.
    using FRtlGetVersion     = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto         ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto         query = ntdll ? reinterpret_cast<FRtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    RTL_OSVERSIONINFOW info{sizeof(RTL_OSVERSIONINFOW)};
    if (!query || query(&info) != 0)
        return "unknown";
    return std::format("{}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}

static std::string bindsData(bool json) {
    std::string result = json ? "[" : "";
    for (const auto& bind : WindowsBinds::defaults()) {
        if (json) {
            if (result.size() > 1)
                result += ", ";
            result += std::format(R"({{"locked": false, "mouse": false, "release": false, "repeat": false, "longPress": false, "non_consuming": false, )"
                                  R"("has_description": true, "modmask": {}, "submap": "", "key": {}, "keycode": 0, "catch_all": false, "description": {}, )"
                                  R"("dispatcher": {}, "arg": {}}})",
                                  WindowsBinds::MODMASK, jsonString(bind.keyName), jsonString(bind.description), jsonString(bind.dispatcher), jsonString(bind.argument));
        } else
            result += std::format("bind\n\tflags: \n\tmodmask: {}\n\tsubmap: \n\tkey: {}\n\tkeycode: 0\n\tcatchall: false\n\tdescription: {}\n\tdispatcher: {}\n\targ: {}\n\n",
                                  WindowsBinds::MODMASK, bind.keyName, bind.description, bind.dispatcher, bind.argument);
    }
    return json ? result + ']' : result;
}

CCommandHandler::CCommandHandler(CWindowManager& manager) : m_manager(manager) {
    std::random_device                    seed;
    std::uniform_int_distribution<size_t> pick(0, NSplashes::SPLASHES.size() - 1);
    m_splash = NSplashes::SPLASHES[pick(seed)];
}

SReply CCommandHandler::execute(const std::string& request) {
    try {
        if (request.find('\0') != std::string::npos)
            return {false, "NUL is not permitted"};
        wide(request); // Validate UTF-8 before processing any mutation.
        const auto slash = request.find('/');
        if (slash == std::string::npos)
            return {false, "Malformed request; expected [flags]/command"};
        bool json = false;
        for (const char flag : std::string_view(request).substr(0, slash)) {
            if (flag != 'j')
                return {false, std::format("Unsupported request flag: {}", flag)};
            json = true;
        }
        const auto [command, rest] = split(std::string_view(request).substr(slash + 1));
        if (command == "keyword") {
            const auto [name, value] = split(rest);
            return keyword(name, value);
        }
        if (command == "getoption") {
            const auto [name, extra] = split(rest);
            if (name.empty() || !extra.empty())
                return {false, "getoption requires exactly one option"};
            return getOption(name, json);
        }
        if (command == "dispatch") {
            const auto [dispatcher, argument] = split(rest);
            return dispatch(dispatcher, argument);
        }
        return query(command, rest, json);
    } catch (const std::exception& error) { return {false, error.what()}; }
}

SReply CCommandHandler::keyword(std::string_view name, std::string_view value) {
    if (name == "general:layout") {
        if (value != "dwindle" && value != "master")
            return {false, "general:layout must be dwindle or master"};
        m_manager.setLayout(value == "master" ? ELayout::Master : ELayout::Dwindle);
    } else if (name == "general:gaps_in" || name == "general:gaps_out") {
        const auto gap = number<int>(value);
        if (!gap || *gap < 0 || *gap > 64)
            return {false, "Gap must be one integer from 0 to 64 DIP (per-side gaps are not supported yet)"};
        if (name == "general:gaps_in")
            m_manager.setGaps(*gap, m_manager.outerGap());
        else
            m_manager.setGaps(m_manager.gap(), *gap);
    } else
        return {false, std::format("Unsupported keyword: {}", name)};
    m_setByUser.emplace(name);
    return {true, "ok", true};
}

SReply CCommandHandler::getOption(std::string_view name, bool json) const {
    const bool set = m_setByUser.contains(name);
    if (name == "general:layout") {
        const std::string_view layout = m_manager.layout() == ELayout::Master ? "master" : "dwindle";
        if (json)
            return {true, std::format(R"({{"option": "{}", "str": "{}", "set": {} }})", name, layout, jsonBool(set))};
        return {true, std::format("str: {}\nset: {}", layout, jsonBool(set))};
    }
    if (name == "general:gaps_in" || name == "general:gaps_out") {
        // Hyprland stores gaps as CSS data (top right bottom left); Windows gaps are uniform.
        const auto css = std::format("{0} {0} {0} {0}", name == "general:gaps_in" ? m_manager.gap() : m_manager.outerGap());
        if (json)
            return {true, std::format(R"({{"option": "{}", "custom": "{}", "set": {} }})", name, css, jsonBool(set))};
        return {true, std::format("custom type: {}\nset: {}", css, jsonBool(set))};
    }
    return {false, "no such option"};
}

SReply CCommandHandler::dispatch(std::string_view dispatcher, std::string_view argument) {
    const auto target = [this, argument] {
        const auto window = findWindow(m_manager.snapshot(), argument);
        if (!window)
            throw std::invalid_argument("No matching tracked window");
        return *window;
    };
    const bool selects = dispatcher == "togglefloating" || dispatcher == "setfloating" || dispatcher == "settiled" || dispatcher == "focuswindow" || dispatcher == "closewindow" ||
        dispatcher == "movefocus";
    if (!selects && !argument.empty())
        return {false, std::format("{} takes no arguments", dispatcher)};
    if (dispatcher == "togglemanagement" || dispatcher == "enable" || dispatcher == "disable")
        m_manager.setEnabled(dispatcher == "enable" || (dispatcher == "togglemanagement" && !m_manager.enabled()));
    else if (dispatcher == "cyclelayout")
        m_manager.setLayout(m_manager.layout() == ELayout::Master ? ELayout::Dwindle : ELayout::Master);
    else if (dispatcher == "togglefloating" || dispatcher == "setfloating" || dispatcher == "settiled") {
        const auto window   = target();
        const bool floating = dispatcher == "togglefloating" ? !window.floating : dispatcher == "setfloating";
        if (window.floating != floating && !m_manager.toggleFloating(window.hwnd))
            return {false, "No matching tracked window"};
    } else if (dispatcher == "movefocus") {
        const auto index = std::string_view("lrud").find(argument);
        if (argument.size() != 1 || index == std::string_view::npos)
            return {false, "Expected movefocus l|r|u|d"};
        if (!m_manager.focus(static_cast<EDirection>(index)))
            return {false, "No directional target or Windows declined focus"};
    } else if (dispatcher == "focuswindow") {
        if (argument.empty())
            return {false, "focuswindow requires a window selector"};
        if (!m_manager.focusWindow(target().hwnd))
            return {false, "Windows declined the foreground activation request"};
    } else if (dispatcher == "killactive" || dispatcher == "closewindow") {
        if (!m_manager.closeWindow(target().hwnd))
            return {false, "Window could not be asked to close"};
        return {true, "ok"};
    } else if (dispatcher == "exit")
        return {true, "ok", false, true};
    else
        return {false, std::format("Unsupported Windows dispatcher: {}", dispatcher)};
    return {true, "ok", true};
}

SReply CCommandHandler::query(std::string_view command, std::string_view argument, bool json) const {
    if (command == "monitors") {
        if (!argument.empty() && argument != "all")
            return {false, "monitors takes no arguments or 'all'"};
        const auto  list   = monitors();
        std::string result = json ? "[" : "";
        for (size_t i = 0; i < list.size(); ++i) {
            if (json && i)
                result += ", ";
            result += monitorData(list[i], static_cast<int>(i), json);
        }
        return {true, json ? result + ']' : result};
    }
    if (!argument.empty())
        return {false, std::format("{} takes no arguments", command)};
    if (command == "version")
        return {true, versionData(json)};
    if (command == "splash")
        return {true, m_splash};
    if (command == "layouts")
        return {true, json ? R"(["dwindle", "master"])" : "dwindle\nmaster"};
    if (command == "binds")
        return {true, bindsData(json)};
    if (command == "cursorpos") {
        POINT cursor{};
        if (!GetCursorPos(&cursor))
            return {false, "Cursor position unavailable"};
        return {true, json ? std::format(R"({{"x": {}, "y": {}}})", cursor.x, cursor.y) : std::format("{}, {}", cursor.x, cursor.y)};
    }
    if (command == "instances") {
        const auto pid = GetCurrentProcessId();
        if (json)
            return {true, std::format(R"([{{"instance": "windows", "pid": {}, "pipe": {}}}])", pid, jsonString(pipeName()))};
        return {true, std::format("instance windows:\n\tpid: {}\n\tpipe: {}\n", pid, utf8(pipeName()))};
    }
    if (command == "systeminfo") {
        if (json)
            return {true,
                    std::format(R"({{"platform": "windows", "os": "{}", "architecture": "x64", "backend": "D3D11/DirectComposition", "version": {}}})", osVersion(),
                                versionData(true))};
        return {true, versionData(false) + std::format("\nSystem Information:\nSystem name: Windows\nVersion: {}\nArchitecture: x64\n", osVersion())};
    }
    if (command == "status") {
        const std::string_view layout = m_manager.layout() == ELayout::Master ? "master" : "dwindle";
        if (json)
            return {true,
                    std::format(R"({{"configProvider": "none", "backend": "win32", "enabled": {}, "layout": "{}", "tracked": {}, "managed": {}, "message": {}}})",
                                jsonBool(m_manager.enabled()), layout, m_manager.trackedCount(), m_manager.managedCount(), jsonString(m_manager.status()))};
        return {true,
                std::format("Config provider: none\nBackend: win32\nManagement: {}\nLayout: {}\nTracked windows: {}\nManaged windows: {}\nMessage: {}",
                            m_manager.enabled() ? "enabled" : "disabled", layout, m_manager.trackedCount(), m_manager.managedCount(), utf8(m_manager.status()))};
    }
    if (command == "clients" || command == "activewindow") {
        const auto  list    = monitors();
        const auto  windows = m_manager.snapshot();
        std::string result;
        if (command == "activewindow") {
            const auto active = findWindow(windows, "activewindow");
            if (!active)
                return {true, json ? "{}" : "Invalid"};
            return {true, windowData(*active, monitorID(list, active->monitor), json)};
        }
        for (const auto& record : windows) {
            if (json && !result.empty())
                result += ", ";
            result += windowData(record, monitorID(list, record.monitor), json);
        }
        if (json)
            return {true, '[' + result + ']'};
        return {true, result.empty() ? "no open windows" : result};
    }
    return {false, std::format("Unsupported Windows command: {}", command)};
}
