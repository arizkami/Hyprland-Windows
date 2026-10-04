#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace WindowsPlatform {
    // Parent process ID, or 0 when the process does not exist.
    uint64_t parentProcessId(uint64_t pid);
    // Full UTF-8 path of the process image, with '/' separators.
    std::optional<std::string> executablePath(uint64_t pid);
}
