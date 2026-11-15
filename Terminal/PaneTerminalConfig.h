#pragma once

#include "Config.h"

#include "TerminalView/TerminalView.h"

#include <string>

namespace term
{
// The widget's slice of the user's config, wired to the daemon of the
// machine the panes live on: this one when host is empty, otherwise the
// remote host's (see RemoteHosts.h). How the app builds every TerminalView.
TerminalConfig terminalConfig(const AppConfig& config,
                              const std::string& host = {},
                              const std::string& sessionName = {});
} // namespace term
