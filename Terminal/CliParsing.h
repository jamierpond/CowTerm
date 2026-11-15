#pragma once

#include "NotifyUserProtocol.h"

#include <optional>
#include <string>
#include <vector>

// The pure half of `ct notify-user`: turning arguments into a request.
// Cli.cpp does the I/O around it.
namespace term::cli
{
struct ParsedNotifyUserRequest
{
    std::optional<NotifyUserRequest> request;
    std::string usageError;
};

// defaultPaneId is $COWTERM_PANE, which --pane overrides.
ParsedNotifyUserRequest parseNotifyUserArguments(const std::vector<std::string>& args,
                                                 const std::string& defaultPaneId);

} // namespace term::cli
