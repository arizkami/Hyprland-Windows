#include "Hyprctl.hpp"
#include "IpcTransport.hpp"
#include <windows.h>
#include <stdexcept>
#include <string_view>

static constexpr std::string_view USAGE = R"#(usage: hyprctl [flags] <command> [args...]

commands:
    activewindow        → Gets the active window and its properties
    binds               → Lists all registered binds
    clients             → Lists all tracked windows with their properties
    cursorpos           → Gets the current cursor position in desktop pixels
    dispatch <dispatcher> [args] → Issue a dispatch to call a bind dispatcher
    getoption <option>  → Gets the config option status (values)
    instances           → Lists the running instance with its info
    keyword <name> <value> → Sets a config keyword dynamically
    layouts             → Lists all layouts available
    monitors            → Lists active outputs with their properties
    splash              → Prints the current random splash
    status              → Prints management state and backend
    systeminfo          → Prints system info
    version             → Prints the Hyprland version

dispatchers (Windows):
    togglemanagement, enable, disable, cyclelayout, exit, killactive
    togglefloating|setfloating|settiled [window]
    focuswindow <window>, closewindow [window], movefocus l|r|u|d

    window: activewindow, address:0x..., pid:N, id:N, class:REGEX,
            title:REGEX, or a bare class regex

keywords and options (Windows):
    general:layout dwindle|master, general:gaps_in|gaps_out 0..64

flags:
    -j                  → Output in JSON
    -q | --quiet        → Disable output on success
    -i | --instance     → Use the specified instance (Windows: 0 only)
    --batch             → Execute a batch of commands, separated by ';'.
                          Commands run in order and the batch stops at the
                          first failure.
)#";
// Consoles take UTF-16; redirected output stays UTF-8 like Hyprland's socket replies.
static void print(DWORD stream, const std::string& text) {
    const HANDLE output = GetStdHandle(stream);
    DWORD        mode = 0, written = 0;
    if (!output || output == INVALID_HANDLE_VALUE)
        return;
    if (GetConsoleMode(output, &mode)) {
        const auto wideText = WindowsIPC::wide(text);
        WriteConsoleW(output, wideText.data(), static_cast<DWORD>(wideText.size()), &written, nullptr);
        return;
    }
    WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

int WindowsIPC::runHyprctl(int argc, wchar_t** argv) {
    try {
        bool        json = false, batch = false, quiet = false;
        std::string command;
        for (int i = 1; i < argc; ++i) {
            const auto argument = utf8(argv[i]);
            if (argument == "-h" || argument == "--help") {
                print(STD_OUTPUT_HANDLE, std::string(USAGE));
                return 0;
            }
            if (argument == "--version") {
                print(STD_OUTPUT_HANDLE, "hyprctl (Windows native)\n");
                return 0;
            }
            if (argument == "-j")
                json = true;
            else if (argument == "--batch")
                batch = true;
            else if (argument == "-q" || argument == "--quiet")
                quiet = true;
            else if (argument == "-i" || argument == "--instance") {
                // Windows runs one instance per login session; accept the only valid index.
                if (++i >= argc || (utf8(argv[i]) != "0" && utf8(argv[i]) != "windows"))
                    throw std::runtime_error("Windows has a single instance per login session; use -i 0");
            } else if (command.empty() && argument.starts_with('-'))
                throw std::runtime_error("Unsupported flag: " + argument);
            else {
                if (!command.empty())
                    command += ' ';
                command += argument;
            }
        }
        if (command.empty()) {
            print(STD_ERROR_HANDLE, std::string(USAGE));
            return 1;
        }
        if (command.size() > MAX_REQUEST)
            throw std::runtime_error("Command exceeds 4096 UTF-8 bytes");
        size_t start = 0;
        do {
            const auto end   = batch ? command.find(';', start) : std::string::npos;
            const auto reply = request((json ? "j/" : "/") + command.substr(start, end == std::string::npos ? end : end - start));
            if (!reply.success) {
                print(STD_ERROR_HANDLE, reply.body + '\n');
                return 1;
            }
            if (!quiet)
                print(STD_OUTPUT_HANDLE, reply.body.ends_with('\n') ? reply.body : reply.body + '\n');
            if (end == std::string::npos)
                break;
            start = end + 1;
        } while (start < command.size());
        return 0;
    } catch (const std::exception& error) {
        print(STD_ERROR_HANDLE, std::string("hyprctl: ") + error.what() + '\n');
        return 1;
    }
}
