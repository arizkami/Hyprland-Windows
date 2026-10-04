#include "Spawn.hpp"
#include <windows.h>
#include <map>

static std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int    size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

static std::wstring upper(std::wstring text) {
    CharUpperBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

// Current environment plus overrides; names compare case-insensitively and the
// block is sorted, as CreateProcess requires.
static std::wstring environmentBlock(const std::vector<std::pair<std::string, std::string>>& overrides) {
    std::map<std::wstring, std::wstring> variables;
    if (const auto block = GetEnvironmentStringsW()) {
        for (auto entry = block; *entry; entry += wcslen(entry) + 1) {
            const std::wstring_view text{entry};
            const auto              equals = text.find(L'=', 1);
            if (equals != std::wstring_view::npos)
                variables[upper(std::wstring{text.substr(0, equals)})] = std::wstring{text};
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : overrides) {
        const auto wideName        = wide(name);
        variables[upper(wideName)] = wideName + L'=' + wide(value);
    }
    std::wstring result;
    for (const auto& [_, entry] : variables) {
        result += entry;
        result += L'\0';
    }
    result += L'\0';
    return result;
}

bool WindowsPlatform::needsCommandShell(const std::string& command) {
    return command.find_first_of("&|<>^%\"") != std::string::npos;
}

std::optional<uint64_t> WindowsPlatform::spawnShellCommand(const std::string& command, const std::vector<std::pair<std::string, std::string>>& environment) {
    if (command.find_first_not_of(" \t") == std::string::npos)
        return std::nullopt;

    // /s: strip exactly the outer quotes, keep the command verbatim.
    std::wstring commandLine = needsCommandShell(command) ? L"cmd.exe /d /s /c \"" + wide(command) + L"\"" : wide(command);
    auto         block       = environmentBlock(environment);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // No inherited handles; CREATE_NO_WINDOW keeps a console helper (cmd.exe) hidden.
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | (needsCommandShell(command) ? CREATE_NO_WINDOW : 0);
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, flags, block.data(), nullptr, &startup, &process))
        return std::nullopt;

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return process.dwProcessId;
}
