#pragma once

#include "RudderProtocol.h"

#include <optional>
#include <string>
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
};

struct RudderTab
{
    std::string name;

    // SessionManager's key (project dir, or name): a place with this path is
    // already open as this tab.
    std::string key;
    std::vector<RudderPane> panes;
};

// One context per pane, then one per place (a ctrl+a f directory) that no tab
// or pane is in yet. Places are pushed with open=false, since activating one
// creates a session. `home` is abbreviated to ~ in titles.
std::vector<rudder::protocol::Context>
    rudderContexts(const std::vector<RudderTab>& tabs,
                   const std::vector<std::string>& placePaths,
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
