// Windows platform layer used by upstream sources: compat POSIX functions and process spawning.
#include "../platform/win32/Spawn.hpp"
#include <sys/utsname.h>
#include <uuid/uuid.h>
#include <glob.h>
#include <windows.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

using namespace std::chrono_literals;

TEST(Posix, ClockMonotonicTracksSteadyClock) {
    timespec   mono{};
    const auto before = std::chrono::steady_clock::now().time_since_epoch();
    ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &mono), 0);
    const auto after = std::chrono::steady_clock::now().time_since_epoch();
    const auto value = std::chrono::seconds(mono.tv_sec) + std::chrono::nanoseconds(mono.tv_nsec);
    EXPECT_GE(value, before);
    EXPECT_LE(value, after);
    EXPECT_LT(mono.tv_nsec, 1000000000L);

    timespec real{};
    ASSERT_EQ(clock_gettime(CLOCK_REALTIME, &real), 0);
    EXPECT_GT(real.tv_sec, 1700000000); // after 2023
    EXPECT_EQ(clock_gettime(42, &real), -1);
}

TEST(Posix, SetenvAndUnsetenv) {
    ASSERT_EQ(setenv("HYPRLAND_COMPAT_TEST", "one", 1), 0);
    EXPECT_STREQ(std::getenv("HYPRLAND_COMPAT_TEST"), "one");
    ASSERT_EQ(setenv("HYPRLAND_COMPAT_TEST", "two", 0), 0); // no overwrite
    EXPECT_STREQ(std::getenv("HYPRLAND_COMPAT_TEST"), "one");
    ASSERT_EQ(unsetenv("HYPRLAND_COMPAT_TEST"), 0);
    EXPECT_EQ(std::getenv("HYPRLAND_COMPAT_TEST"), nullptr);
    EXPECT_EQ(setenv("", "x", 1), -1);
}

TEST(Posix, Uname) {
    utsname name{};
    ASSERT_EQ(uname(&name), 0);
    EXPECT_STREQ(name.sysname, "Windows");
    EXPECT_GT(std::strlen(name.nodename), 0u);
    EXPECT_TRUE(std::string(name.release).starts_with("10.0.")) << name.release;
    EXPECT_STREQ(name.machine, "x86_64");
}

TEST(Posix, UuidIsRandomVersion4) {
    std::set<std::string> seen;
    for (int i = 0; i < 64; ++i) {
        uuid_t value{};
        uuid_generate_random(value);
        EXPECT_EQ(value[6] >> 4, 4);
        EXPECT_EQ(value[8] >> 6, 2);
        seen.emplace(reinterpret_cast<const char*>(value), 16);
    }
    EXPECT_EQ(seen.size(), 64u);
}

TEST(Posix, KillTerminatesAndProbes) {
    STARTUPINFOW        startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    std::wstring        command = L"cmd.exe /d /c ping -n 30 127.0.0.1 >nul";
    ASSERT_TRUE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    const auto pid = static_cast<pid_t>(process.dwProcessId);
    EXPECT_EQ(kill(pid, 0), 0);
    EXPECT_EQ(kill(pid, SIGKILL), 0);
    EXPECT_EQ(WaitForSingleObject(process.hProcess, 5000), WAIT_OBJECT_0);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    EXPECT_EQ(kill(0, SIGKILL), -1);
}

TEST(Spawn, ShellSyntaxDetection) {
    EXPECT_FALSE(WindowsPlatform::needsCommandShell("notepad.exe C:/file.txt"));
    EXPECT_TRUE(WindowsPlatform::needsCommandShell("echo a & echo b"));
    EXPECT_TRUE(WindowsPlatform::needsCommandShell("echo %PATH%"));
    EXPECT_TRUE(WindowsPlatform::needsCommandShell("\"C:/Program Files/App/app.exe\""));
    EXPECT_FALSE(WindowsPlatform::spawnShellCommand("   ", {}).has_value());
}

static std::string waitForFile(const std::filesystem::path& path) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream file(path);
        std::string   content;
        if (file && std::getline(file, content) && !content.empty())
            return content;
        std::this_thread::sleep_for(20ms);
    }
    return {};
}

TEST(Spawn, ShellCommandsSeeAddedEnvironment) {
    const auto path = std::filesystem::temp_directory_path() / ("hyprland-spawn-" + std::to_string(GetCurrentProcessId()) + ".txt");
    std::filesystem::remove(path);
    const auto pid = WindowsPlatform::spawnShellCommand("echo %HL_SPAWN_TEST%> \"" + path.string() + "\"", {{"HL_SPAWN_TEST", "workspace-7"}});
    ASSERT_TRUE(pid.has_value());
    EXPECT_GT(*pid, 0u);
    EXPECT_EQ(waitForFile(path), "workspace-7");
    std::filesystem::remove(path);
}

TEST(Spawn, PlainCommandsReturnTheApplicationPid) {
    const auto pid = WindowsPlatform::spawnShellCommand("ping.exe -n 30 127.0.0.1", {});
    ASSERT_TRUE(pid.has_value());
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(*pid));
    ASSERT_NE(process, nullptr);
    wchar_t image[MAX_PATH]{};
    DWORD   size = MAX_PATH;
    ASSERT_TRUE(QueryFullProcessImageNameW(process, 0, image, &size));
    EXPECT_TRUE(std::wstring(image).ends_with(L"PING.EXE") || std::wstring(image).ends_with(L"ping.exe"));
    TerminateProcess(process, 0);
    CloseHandle(process);
    EXPECT_FALSE(WindowsPlatform::spawnShellCommand("hyprland-no-such-program.exe", {}).has_value());
}

TEST(Posix, GlobMatchesComponentsSortedAndSkipsDotFiles) {
    const auto root = std::filesystem::temp_directory_path() / ("hyprland-glob-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "conf.d" / "nested");
    for (const auto name : {"b.lua", "a.lua", "c.conf", ".hidden.lua"})
        std::ofstream(root / "conf.d" / name) << "x";
    std::ofstream(root / "conf.d" / "nested" / "d.lua") << "x";

    const auto base = root.generic_string();
    glob_t     result{};
    ASSERT_EQ(glob((base + "/conf.d/*.lua").c_str(), GLOB_TILDE, nullptr, &result), 0);
    ASSERT_EQ(result.gl_pathc, 2u);
    EXPECT_EQ(std::string(result.gl_pathv[0]), base + "/conf.d/a.lua");
    EXPECT_EQ(std::string(result.gl_pathv[1]), base + "/conf.d/b.lua");
    EXPECT_EQ(result.gl_pathv[2], nullptr);
    globfree(&result);

    ASSERT_EQ(glob((base + "/*/n?sted/[a-d].LUA").c_str(), 0, nullptr, &result), 0); // case-insensitive
    ASSERT_EQ(result.gl_pathc, 1u);
    EXPECT_EQ(std::string(result.gl_pathv[0]), base + "/conf.d/nested/d.lua");
    globfree(&result);

    ASSERT_EQ(glob((base + "/conf.d/[!ab]*").c_str(), GLOB_MARK, nullptr, &result), 0);
    ASSERT_EQ(result.gl_pathc, 2u);
    EXPECT_EQ(std::string(result.gl_pathv[0]), base + "/conf.d/c.conf");
    EXPECT_EQ(std::string(result.gl_pathv[1]), base + "/conf.d/nested/");
    globfree(&result);

    EXPECT_EQ(glob((base + "/conf.d/*.none").c_str(), 0, nullptr, &result), GLOB_NOMATCH);
    ASSERT_EQ(glob((base + "\\conf.d\\a.lua").c_str(), 0, nullptr, &result), 0); // literal, backslashes
    EXPECT_EQ(result.gl_pathc, 1u);
    globfree(&result);

    ASSERT_EQ(glob("~", GLOB_TILDE, nullptr, &result), 0);
    EXPECT_EQ(result.gl_pathc, 1u);
    globfree(&result);
    std::filesystem::remove_all(root);
}
