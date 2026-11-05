#pragma once

#include "BoardDocument.h"
#include "Zones.h"

#include <Background.h>
#include <ClaudeLaunch.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
// What the card's sessions are doing right now, as claude reports it.
enum class SessionActivity
{
    needsYou,
    busy,
    idle
};

std::string_view getSessionActivityName(SessionActivity activity);

struct TaskCard
{
    std::string key;
    std::string taskName;
    std::string workingDirectory;
    std::string gitBranch;
    std::string badgeText;
    std::string description;
    std::string latestActivityTimestamp;
    // The CowTerm pane (its shell id) running the card's most urgent session.
    std::string paneShellId;
    // Every claude session id grouped into the card.
    std::vector<std::string> sessionIds;
    Zone zone = Zone::working;
    SessionActivity activity = SessionActivity::idle;
    int sessionCount = 0;
};

struct BoardSnapshot
{
    std::vector<TaskCard> cards;
    std::string problem;
};

// A CowTerm pane as the board needs it, copied on the main thread.
struct BoardPane
{
    std::string shellId;
    std::string workingDirectory;
    bool isRunningClaude = false;
};

bool isProcessIdRunning(std::int64_t processId);

struct BoardQuery
{
    claudecli::ClaudeLaunch launch;
    std::string configDirectory;
    std::vector<BoardPane> panes;

    // Whether a process with this id is running. claude agents can still list a
    // session whose process was killed; such a session gets no card.
    std::function<bool(std::int64_t processId)> isProcessRunning = isProcessIdRunning;

    // The CowTerm pane id a process inherited (its COWTERM_PANE), if any.
    std::function<std::optional<std::string>(std::int64_t processId)> findPaneOfProcess;
};

std::string makeCardKey(std::string_view workingDirectory, std::string_view gitBranch);

// Blocks: runs `claude agents --json --all` and reads the transcripts of every
// session found in a pane. Call it off the main thread.
BoardSnapshot collectBoardSnapshot(const BoardQuery& query);

// The same from an agent listing already in hand (tests and snapshots).
BoardSnapshot buildBoardSnapshot(const BoardQuery& query,
                                 const claudecli::AgentListing& listing);

// Gives every card its stored zone, unless claude says it needs Jamie: that
// always shows in needs you.
void applyBoardDocument(BoardSnapshot& snapshot, const BoardDocument& document);

// The live card a claude session is part of, if any.
std::optional<TaskCard> findCardOfSession(const BoardSnapshot& snapshot, const std::string& sessionId);

// Needs-you cards first, then newest activity.
std::vector<TaskCard> getCardsInZone(const BoardSnapshot& snapshot, Zone zone);
} // namespace term
