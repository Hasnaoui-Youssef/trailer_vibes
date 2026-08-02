#include "resource_paths.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace providers::device {

namespace {

std::filesystem::path ExecutablePath() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) return {};
    return std::filesystem::path(buffer, buffer + length);
#else
    char buffer[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return {};
    buffer[length] = '\0';
    return std::filesystem::path(buffer);
#endif
}

}  // namespace

std::filesystem::path ResourcesRoot() {
    const std::filesystem::path exe = ExecutablePath();
    if (exe.empty()) return {};
    return exe.parent_path() / "resources";
}

}  // namespace providers::device
