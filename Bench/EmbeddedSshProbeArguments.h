#pragma once

#include "Multiplexer/EmbeddedSshLink.h"

#include <optional>
#include <string>
#include <vector>

enum class ProbeMode
{
    // Open a throwaway pane, echo into it, print it and kill it.
    ThrowawayPane,

    // Print an existing pane read only and release it.
    AdoptPane,

    // Open a pane with this id running `seq 1 300` and leave it running.
    LeavePaneRunning,

    // Kill this pane (one LeavePaneRunning made).
    KillPane,

    // Open a pane with this id at 150 columns running `seq 1 300`, take its
    // size, and print every size-owner notice until stopped.
    WatchPane,

    // Ask the daemon to quit (a test instance, never the user's own).
    QuitServer
};

// CowTermEmbeddedSshProbe's command line:
//   <user@host[:port]>                       ThrowawayPane
//   <user@host[:port]> --adopt [pane]        AdoptPane
//   <user@host[:port]> --leave-pane <pane>   LeavePaneRunning
//   <user@host[:port]> --kill-pane <pane>    KillPane
//   <user@host[:port]> --watch-pane <pane>   WatchPane
//   <user@host[:port]> --quit-server         QuitServer
struct ProbeArguments
{
    term::SshDestination destination;
    ProbeMode mode = ProbeMode::ThrowawayPane;

    // Empty with --adopt: the first pane the daemon lists.
    std::string paneId;
};

// Empty when the arguments don't match the usage above.
std::optional<ProbeArguments> parseProbeArguments(const std::vector<std::string>& arguments);
