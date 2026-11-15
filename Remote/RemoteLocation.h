#pragma once

#include <optional>
#include <string>
#include <vector>

namespace term::remote
{
// A searchDirs entry on another machine: `alias:path`, alias an ssh
// alias from ~/.ssh/config, handed to ssh exactly as written.
struct RemoteSearchDir
{
    std::string host;
    std::string path;
};

// What CowTerm runs on a remote host through ssh: the headless install's ct
// when there is one, otherwise a full install's ct on PATH's usual spot.
inline constexpr auto bridgeCommand =
    "ct=\"$HOME/.local/libexec/cowterm-remote/ct\"; "
    "[ -x \"$ct\" ] || [ -x \"$ct.exe\" ] || ct=\"$HOME/.local/bin/ct\"; "
    "exec \"$ct\" bridge";

// Empty for a local entry ("~/projects", "/abs", "C:\\x").
std::optional<RemoteSearchDir> parseRemoteSearchDir(const std::string& entry);

// Every distinct host named in searchDirs, in first-seen order.
std::vector<std::string> collectRemoteHosts(const std::vector<std::string>& searchDirs);

// "tamby:/home/x/Grazer" for a project dir on a host; plain paths stay local.
std::string makeRemoteProjectDir(const std::string& host, const std::string& path);
} // namespace term::remote
