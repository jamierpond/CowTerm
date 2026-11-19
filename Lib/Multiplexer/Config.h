#pragma once

#include "Emulator/TermTypes.h"

#include <Miro/Reflect.h>

#include <map>
#include <string>
#include <vector>

namespace term
{
// Per-machine settings for remote searchDirs hosts, keyed by host as
// written there.
struct HostSettings
{
    std::string color;

    MIRO_REFLECT(color)
};

// User configuration, read from ~/.config/cowterm.json. Unknown keys are
// ignored and missing keys keep their defaults, so the file can be shared
// with other tools and grown over time.
struct AppConfig
{
    // Local paths, or alias:path for another machine (an ~/.ssh/config alias).
    std::vector<std::string> searchDirs = {"~/projects", "~"};
    std::string font = "JetBrains Mono";
    float fontSize = 13.0f;
    std::string theme = "rosepine";
    std::map<std::string, HostSettings> hosts;

    // Where this machine's daemon sends a Claude's finishing or asking, to
    // ring the phone ("http://macprorack:8742"); empty sends nothing.
    std::string pushRelay;

    // This machine's name as the phone dials it ("big-cow"): its sessions'
    // colour, and the title of its notifications. Empty means its short host
    // name; both Macs' host names read "daily-driver", so each sets it.
    std::string hostName;

    MIRO_REFLECT(searchDirs, font, fontSize, theme, hosts, pushRelay, hostName)
};

AppConfig loadConfig();

// Writes the effective config back to ~/.config/cowterm.json, so settings
// changed from inside the app (Cmd +/- zoom) survive a relaunch. Best
// effort: an unwritable file loses the change but never interrupts typing.
void saveConfig(const AppConfig& config);

// The config as the text saveConfig writes over existingText: the fields this
// build knows, plus every other top-level key existingText had, so a setting a
// newer or other build added (pushRelay, hostName) survives a save.
std::string writeConfigKeepingUnknownSettings(const AppConfig& config, const std::string& existingText);

Theme themeByName(const std::string& name);

// "~/x" -> "$HOME/x"
std::string expandHome(const std::string& path);
} // namespace term
