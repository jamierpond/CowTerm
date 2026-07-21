#pragma once

#include "TermTypes.h"

#include <Miro/Reflect.h>

#include <string>
#include <vector>

namespace term
{
// One Ctrl+A leader binding: `key` is the character typed after the
// prefix. Exactly one action should be set — `send` types text into the
// active pane ("\n" presses Enter, tmux send-keys), `popup` runs a command
// in a full-window popup over the session (tmux display-popup -E).
struct KeyBinding
{
    std::string key;
    std::string send;
    std::string popup;

    MIRO_REFLECT(key, send, popup)
};

// User configuration, read from ~/.config/cowterm.json. Unknown keys are
// ignored and missing keys keep their defaults, so the file can be shared
// with other tools and grown over time.
struct AppConfig
{
    std::vector<std::string> searchDirs = {"~/projects", "~"};
    std::string font = "JetBrains Mono";
    float fontSize = 13.0f;
    std::string theme = "rosepine";

    // Config bindings run before the built-in leader table, so a binding
    // here can also re-purpose a built-in key.
    std::vector<KeyBinding> bindings = {{.key = "u", .send = "cd ..\n"},
                                        {.key = "n", .send = "nvim .\n"}};

    // The web UI's HTTP port (loopback only; the WebSocket stream rides one
    // port up). 0 disables the gateway. 2697 spells COWS on a phone pad.
    int webPort = 2697;

    MIRO_REFLECT(searchDirs, font, fontSize, theme, bindings, webPort)
};

inline constexpr float minFontSize = 7.0f;
inline constexpr float maxFontSize = 40.0f;

AppConfig loadConfig();

// Writes the effective config back to ~/.config/cowterm.json, so settings
// changed from inside the app (Cmd +/- zoom) survive a relaunch. Best
// effort: an unwritable file loses the change but never interrupts typing.
void saveConfig(const AppConfig& config);

Theme themeByName(const std::string& name);

// "~/x" -> "$HOME/x"
std::string expandHome(const std::string& path);
} // namespace term
