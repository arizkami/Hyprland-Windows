#include "Posix.hpp"
#include "include/sys/utsname.h"
#include "include/uuid/uuid.h"
#include "include/glob.h"
#include "include/sys/mman.h"
#include "include/sys/eventfd.h"
#include "include/sys/ioctl.h"
#include <fcntl.h>
#include <io.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

template <typename Clock>
static void fill(timespec* out) {
    const auto since = Clock::now().time_since_epoch();
    const auto secs  = std::chrono::duration_cast<std::chrono::seconds>(since);
    out->tv_sec      = static_cast<time_t>(secs.count());
    out->tv_nsec     = static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(since - secs).count());
}

int clock_gettime(clockid_t clock, timespec* out) {
    if (!out) {
        errno = EFAULT;
        return -1;
    }
    if (clock == CLOCK_MONOTONIC)
        fill<std::chrono::steady_clock>(out);
    else if (clock == CLOCK_REALTIME)
        fill<std::chrono::system_clock>(out);
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (!name || !*name || !value) {
        errno = EINVAL;
        return -1;
    }
    size_t existing = 0;
    if (!overwrite && getenv_s(&existing, nullptr, 0, name) == 0 && existing > 0)
        return 0;
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

int unsetenv(const char* name) {
    if (!name || !*name) {
        errno = EINVAL;
        return -1;
    }
    // An empty value removes the variable from the CRT environment.
    return _putenv_s(name, "") == 0 ? 0 : -1;
}

int kill(pid_t pid, int signal) {
    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    const DWORD access  = signal == 0 ? PROCESS_QUERY_LIMITED_INFORMATION : PROCESS_TERMINATE;
    const HANDLE process = OpenProcess(access, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        errno = GetLastError() == ERROR_ACCESS_DENIED ? EPERM : ESRCH;
        return -1;
    }
    BOOL ok = TRUE;
    if (signal == SIGKILL || signal == SIGTERM)
        ok = TerminateProcess(process, 128 + signal);
    else if (signal != 0) {
        CloseHandle(process);
        errno = EINVAL;
        return -1;
    }
    CloseHandle(process);
    if (!ok) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int uname(struct utsname* name) {
    if (!name) {
        errno = EFAULT;
        return -1;
    }
    std::memset(name, 0, sizeof(*name));
    std::snprintf(name->sysname, sizeof(name->sysname), "Windows");

    DWORD size = sizeof(name->nodename);
    if (!GetComputerNameA(name->nodename, &size))
        std::snprintf(name->nodename, sizeof(name->nodename), "unknown");

    // GetVersionEx reports the manifest-compatible version; RtlGetVersion reports the real one.
    using FRtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW info{sizeof(RTL_OSVERSIONINFOW)};
    const auto         ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto         query = ntdll ? reinterpret_cast<FRtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    if (query && query(&info) == 0) {
        std::snprintf(name->release, sizeof(name->release), "%lu.%lu.%lu", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
        DWORD ubr = 0, ubrSize = sizeof(ubr);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &ubrSize) == ERROR_SUCCESS)
            std::snprintf(name->version, sizeof(name->version), "%lu.%lu", info.dwBuildNumber, ubr);
    }

    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    const char* machine = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64" :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64                   ? "aarch64" :
                                                                                          "unknown";
    std::snprintf(name->machine, sizeof(name->machine), "%s", machine);
    return 0;
}

void uuid_generate_random(uuid_t out) {
    BCryptGenRandom(nullptr, out, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    out[6] = static_cast<unsigned char>((out[6] & 0x0f) | 0x40); // version 4
    out[8] = static_cast<unsigned char>((out[8] & 0x3f) | 0x80); // RFC 9562 variant
}

// glob ------------------------------------------------------------------------

static bool hasWildcard(const std::string& component) {
    return component.find_first_of("*?[") != std::string::npos;
}

// NTFS names compare case-insensitively, so matching does too.
static wchar_t fold(wchar_t c) {
    return static_cast<wchar_t>(std::towlower(c));
}

// fnmatch for one path component.
static bool matchComponent(const std::wstring& pattern, size_t p, const std::wstring& name, size_t n) {
    while (p < pattern.size()) {
        const wchar_t c = pattern[p];
        if (c == L'*') {
            while (p < pattern.size() && pattern[p] == L'*')
                ++p;
            if (p == pattern.size())
                return true;
            for (size_t i = n; i <= name.size(); ++i) {
                if (matchComponent(pattern, p, name, i))
                    return true;
            }
            return false;
        }
        if (n == name.size())
            return false;
        if (c == L'?') {
            ++p;
            ++n;
            continue;
        }
        if (c == L'[') {
            const size_t close = pattern.find(L']', p + 2);
            if (close == std::wstring::npos) {
                if (fold(name[n]) != L'[')
                    return false;
                ++p;
                ++n;
                continue;
            }
            size_t     i       = p + 1;
            const bool negate  = pattern[i] == L'!' || pattern[i] == L'^';
            bool       matched = false;
            if (negate)
                ++i;
            for (; i < close; ++i) {
                if (i + 2 < close && pattern[i + 1] == L'-') {
                    matched |= fold(name[n]) >= fold(pattern[i]) && fold(name[n]) <= fold(pattern[i + 2]);
                    i += 2;
                } else
                    matched |= fold(name[n]) == fold(pattern[i]);
            }
            if (matched == negate)
                return false;
            p = close + 1;
            ++n;
            continue;
        }
        if (fold(c) != fold(name[n]))
            return false;
        ++p;
        ++n;
    }
    return n == name.size();
}

static std::filesystem::path fromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

static std::string toUtf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

int glob(const char* pattern, int flags, int (*errfunc)(const char*, int), glob_t* result) {
    if (!pattern || !result)
        return GLOB_ABORTED;
    *result = glob_t{};

    std::string text = pattern;
    std::replace(text.begin(), text.end(), '\\', '/');
    if ((flags & GLOB_TILDE) && text.starts_with('~') && (text.size() == 1 || text[1] == '/')) {
        const char* home = std::getenv("HOME");
        if (!home || !*home)
            home = std::getenv("USERPROFILE");
        if (home) {
            std::string expanded = home;
            std::replace(expanded.begin(), expanded.end(), '\\', '/');
            text = expanded + text.substr(1);
        }
    }

    // The root prefix ("C:/", "C:", "/") is kept verbatim.
    std::string prefix;
    if (text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':')
        prefix = text.size() >= 3 && text[2] == '/' ? text.substr(0, 3) : text.substr(0, 2);
    else if (text.starts_with('/'))
        prefix = "/";

    std::vector<std::string> components;
    for (size_t start = prefix.size(); start <= text.size();) {
        const size_t end  = text.find('/', start);
        const auto   part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty())
            components.push_back(part);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    std::vector<std::string> current{prefix};
    for (size_t i = 0; i < components.size() && !current.empty(); ++i) {
        const bool               last = i + 1 == components.size();
        std::vector<std::string> next;
        for (const auto& base : current) {
            const std::string joined = base.empty() || base.ends_with('/') ? base : base + "/";
            if (!hasWildcard(components[i])) {
                const auto      candidate = joined + components[i];
                std::error_code ec;
                const auto      status = std::filesystem::status(fromUtf8(candidate), ec);
                if (!ec && (last ? std::filesystem::exists(status) : std::filesystem::is_directory(status)))
                    next.push_back(candidate);
                continue;
            }

            std::error_code ec;
            auto            it = std::filesystem::directory_iterator(fromUtf8(base.empty() ? "." : base), ec);
            if (ec) {
                if ((errfunc && errfunc(base.c_str(), ec.value())) || (flags & GLOB_ERR))
                    return GLOB_ABORTED;
                continue;
            }
            const auto wpattern = fromUtf8(components[i]).wstring();
            for (const auto& entry : it) {
                const auto name = entry.path().filename().wstring();
                // As in POSIX, wildcards never match a leading '.'.
                if (name.starts_with(L'.') && !wpattern.starts_with(L'.'))
                    continue;
                if (!matchComponent(wpattern, 0, name, 0))
                    continue;
                std::error_code typeError;
                if (!last && !entry.is_directory(typeError))
                    continue;
                next.push_back(joined + toUtf8(entry.path().filename()));
            }
        }
        current = std::move(next);
    }

    if (components.empty())
        current.clear();
    if (current.empty()) {
        if (!(flags & GLOB_NOCHECK))
            return GLOB_NOMATCH;
        current.push_back(pattern);
    }
    if (!(flags & GLOB_NOSORT))
        std::sort(current.begin(), current.end());

    result->gl_pathv = static_cast<char**>(std::calloc(current.size() + 1, sizeof(char*)));
    if (!result->gl_pathv)
        return GLOB_NOSPACE;
    for (const auto& path : current) {
        std::string     value = path;
        std::error_code ec;
        if ((flags & GLOB_MARK) && std::filesystem::is_directory(fromUtf8(path), ec))
            value += '/';
        auto copy = static_cast<char*>(std::malloc(value.size() + 1));
        if (!copy) {
            globfree(result);
            return GLOB_NOSPACE;
        }
        std::memcpy(copy, value.c_str(), value.size() + 1);
        result->gl_pathv[result->gl_pathc++] = copy;
    }
    return 0;
}

void globfree(glob_t* result) {
    if (!result || !result->gl_pathv)
        return;
    for (size_t i = 0; i < result->gl_pathc; ++i)
        std::free(result->gl_pathv[i]);
    std::free(result->gl_pathv);
    *result = glob_t{};
}

void* mmap(void*, size_t, int, int, int, long long) {
    errno = ENOSYS;
    return MAP_FAILED;
}

int munmap(void*, size_t) {
    errno = ENOSYS;
    return -1;
}

int eventfd(unsigned int, int) {
    errno = ENOSYS;
    return -1;
}

int ioctl(int, unsigned long, ...) {
    errno = ENOTTY;
    return -1;
}

extern "C" int pipe(int fds[2]) {
    return _pipe(fds, 65536, _O_BINARY | _O_NOINHERIT);
}
