#pragma once

#include "RudderProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace term
{
// What CowTerm recorded about the user's use of a session or a place, under
// its key: the last switch to it (MruStore) and the palette's decayed use
// count (PaletteUsageStore) with the time that count is as of. 0 where CowTerm
// has no record.
struct RecordedUse
{
    std::int64_t lastUsedAtMs = 0;
    double useCount = 0;
    std::int64_t useCountAtMs = 0;
};

// What Rudder (the app switcher) is told about one pane: plain values copied
// out of the live tree, so the context list can be built and tested without
// a window.
struct RudderPane
{
    std::string shellId;
    std::string title;
    std::string command;
    std::string cwd;

    // The git branch checked out at cwd; empty outside a repo or on a
    // detached HEAD. The pane's name in Rudder when it has one.
    std::string branch;

    // The repository root holding cwd; empty outside a repo.
    std::string repo;

    // The pane CowTerm is showing: the active tab's focused pane.
    bool focused = false;

    // When the user last typed in the pane and when a file last changed in
    // its project, as its daemon last reported them; 0 where it reported none.
    std::int64_t lastTypedAtMs = 0;
    std::int64_t lastFileChangeAtMs = 0;
};

struct RudderTab
{
    std::string name;

    // SessionManager's key (project dir, or name): a place with this path is
    // already open as this tab.
    std::string key;
    std::vector<RudderPane> panes;
    RecordedUse use;
};

// A ctrl+a f directory, and the branch and repository root there if it's a
// repo.
struct RudderPlace
{
    RudderPlace(std::string pathToUse,
                std::string branchToUse = {},
                std::string repoToUse = {})
        : path(std::move(pathToUse))
        , branch(std::move(branchToUse))
        , repo(std::move(repoToUse))
    {
    }

    std::string path;
    std::string branch;
    std::string repo;
    RecordedUse use;
};

// One context per pane, then one per place (a ctrl+a f directory) that no tab
// or pane is in yet. Places are pushed with open=false, since activating one
// creates a session. Each carries its directory as `path` (what Rudder
// matches a project name on) and its git branch; `home` is abbreviated to ~
// where it's shown. Each also carries as `usage` what CowTerm recorded about
// its use: a pane its session's RecordedUse and its own typing and file
// change times, a place its own RecordedUse; left out where there is none.
std::vector<rudder::protocol::Context>
    rudderContexts(const std::vector<RudderTab>& tabs,
                   const std::vector<RudderPlace>& places,
                   const std::string& home);

// Context ids: "pane:<shellId>" and "place:<abs path>".
struct RudderTarget
{
    enum class Kind
    {
        Pane,
        Place
    };

    Kind kind = Kind::Pane;
    std::string value;
};

std::optional<RudderTarget> parseRudderContextId(const std::string& id);
std::string paneContextId(const std::string& shellId);
} // namespace term
