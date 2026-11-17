#pragma once

#include "Multiplexer/EmbeddedSshLink.h"

#include <optional>
#include <string>
#include <vector>

// CowTermEmbeddedSshProbe's command line:
//   <user@host[:port]>                  a throwaway pane of its own
//   <user@host[:port]> --adopt [pane]   an existing pane, read only
struct ProbeArguments
{
    term::SshDestination destination;
    bool isAdoptingAPane = false;

    // Empty with --adopt: the first pane the daemon lists.
    std::string adoptedPaneId;
};

// Empty when the arguments don't match the usage above.
std::optional<ProbeArguments> parseProbeArguments(const std::vector<std::string>& arguments);
