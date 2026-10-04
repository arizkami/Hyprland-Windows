#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace WindowsPlatform {
    // Starts `command` detached, with `environment` added to the current environment.
    // Plain commands run directly, so the returned PID is the application's own (exec
    // rules match windows by PID). Commands using cmd.exe syntax (& | < > ^ % ")
    // run through `cmd.exe /d /s /c`. Returns the PID, or nothing on failure.
    std::optional<uint64_t> spawnShellCommand(const std::string& command, const std::vector<std::pair<std::string, std::string>>& environment);

    // True when the command needs cmd.exe to interpret it.
    bool needsCommandShell(const std::string& command);
}
