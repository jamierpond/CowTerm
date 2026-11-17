#pragma once

#include "Emulator/TermState.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace term::phone
{
// The last screen of the pane the phone showed, kept so the next launch can
// draw it at once, dimmed, before the link is up ("Leaving the desk", step 2).
struct StoredScreen
{
    std::string host;
    std::string paneId;
    std::string sessionName;
    std::int64_t savedAtMillisecondsSinceEpoch = 0;
    TerminalState state;
};

// Replaces the file in one step, so a crash mid-save leaves the old screen.
// False when it could not be written.
bool saveStoredScreen(const std::filesystem::path& path, const StoredScreen& screen);

// Empty when there is no file, or it does not parse as one this build wrote.
std::optional<StoredScreen> loadStoredScreen(const std::filesystem::path& path);

// "12 s ago", "4 min ago", "3 h ago", "2 d ago"; "just now" under a second.
std::string describeStoredScreenAge(std::int64_t ageMilliseconds);

// When a session was last used: "just now" under a minute, "yesterday" from
// one day to two, else as describeStoredScreenAge ("2 min ago", "3 h ago", "4 d ago").
std::string describeLastUseAge(std::int64_t ageMilliseconds);
} // namespace term::phone
