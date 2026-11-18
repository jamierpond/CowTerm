#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

// What the pane screen is told about the pane it shows. The connection layer
// fills these; until it lands, Main.cpp fills them with sample values.
namespace cowterm::mobile
{
enum class LinkState
{
    Live,
    Connecting,
    Stalled,
    Unreachable
};

struct LinkStatus
{
    LinkState state = LinkState::Connecting;

    // When the screen on show was last brought up to date: the age shown on a
    // pane that is not live.
    std::chrono::steady_clock::time_point pictureTakenAt =
        std::chrono::steady_clock::now();
};

struct PaneDescription
{
    std::string hostName;
    std::string sessionName;

    // The session he was in before this one; nothing on the first.
    std::optional<std::string> previousSessionName;

    int paneCount = 1;
    int activePaneIndex = 0;

    // Other sessions with a pane Jamie has not seen (the count on ☰), and this
    // session's panes he has not seen (their dots are yellow squares).
    int unreadSessionsElsewhere = 0;
    std::vector<int> unreadPaneIndexes;
};
} // namespace cowterm::mobile
