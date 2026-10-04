#include "ProcessInfo.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>

uint64_t WindowsPlatform::parentProcessId(uint64_t pid) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize    = sizeof(entry);
    uint64_t parent = 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == pid) {
            parent = entry.th32ParentProcessID;
            break;
        }
    }
    CloseHandle(snapshot);
    return parent;
}

std::optional<std::string> WindowsPlatform::executablePath(uint64_t pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return std::nullopt;
    std::wstring path(32768, L'\0');
    DWORD        size = static_cast<DWORD>(path.size());
    const BOOL   ok   = QueryFullProcessImageNameW(process, 0, path.data(), &size);
    CloseHandle(process);
    if (!ok)
        return std::nullopt;
    path.resize(size);
    const auto utf8 = std::filesystem::path(path).generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}
