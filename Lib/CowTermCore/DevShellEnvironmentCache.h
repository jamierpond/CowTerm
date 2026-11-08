#pragma once

#include "Pty.h"

#include <optional>
#include <string>
#include <vector>

namespace term
{
// The Visual Studio a cached developer environment came from. Any change --
// another install, a VS update, another architecture -- makes the cache stale.
struct VisualStudioInstall
{
    std::string installationPath;
    std::string installationVersion;
    std::string architecture;
};

// The variables Enter-VsDevShell adds or changes (PATH, INCLUDE, LIB, ...),
// recorded once so a new pane can start with them instead of running it.
struct DevShellEnvironmentCache
{
    VisualStudioInstall install;
    std::vector<EnvironmentVariable> variables;
};

// The cache file: a key=value line per VisualStudioInstall field, a blank
// line, then one NAME=VALUE line per variable.
std::string serialiseDevShellEnvironmentCache(const DevShellEnvironmentCache& cache);
std::optional<DevShellEnvironmentCache>
    parseDevShellEnvironmentCache(const std::string& text);

bool isDevShellEnvironmentCacheFor(const DevShellEnvironmentCache& cache,
                                   const VisualStudioInstall& install);

// NAME=VALUE lines, as the capture script prints them. Lines without '=' or
// with an empty name are skipped.
std::vector<EnvironmentVariable> parseEnvironmentVariableLines(const std::string& text);

// vswhere's default text output ("installationPath: C:\..." lines) for one
// instance. Empty when either the path or the version is missing.
std::optional<VisualStudioInstall> parseVswhereOutput(const std::string& text,
                                                      const std::string& architecture);
} // namespace term
