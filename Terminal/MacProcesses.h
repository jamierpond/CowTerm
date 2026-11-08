#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace term::macos
{
struct ProcessOutput
{
    int status = -1;

    // stdout and stderr, merged.
    std::string text;
};

enum class StandardInput
{
    inherited,
    discarded
};

// This executable, symlinks resolved, so ~/.local/bin/ct names the app's binary.
std::filesystem::path getExecutablePath();

// The .app that contains this executable, if any.
std::optional<std::filesystem::path> findAppBundlePath();

// argv for posix_spawn: pointers into args, null-terminated. Valid while args lives.
std::vector<char*> createArgumentVector(const std::vector<std::string>& args);

// Runs a program to completion and returns what it printed.
ProcessOutput runProcessCapturingOutput(const std::vector<std::string>& args,
                                        StandardInput input);
} // namespace term::macos
