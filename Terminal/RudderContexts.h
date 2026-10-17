#pragma once

#include "RudderProtocol.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace term
{
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

    // The pane CowTerm is showing: the active tab's focused pane.
    bool focused = false;
};

struct RudderTab
{
    std::string name;

    // SessionManager's key (project dir, or name): a place with this path is
    // already open as this tab.
    std::string key;
    std::vector<RudderPane> panes;
};

// A ctrl+a f directory, and the branch checked out there if it's a repo.
struct RudderPlace
{
    RudderPlace(std::string pathToUse, std::string branchToUse = {})
        : path(std::move(pathToUse))
        , branch(std::move(branchToUse))
    {
    }

    std::string path;
    std::string branch;
};

// One context per pane, then one per place (a ctrl+a f directory) that no tab
// or pane is in yet. Places are pushed with open=false, since activating one
// creates a session. Each carries its directory as `path` (what Rudder
// matches a project name on) and its git branch; `home` is abbreviated to ~
// where it's shown.
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
} // namespace term
