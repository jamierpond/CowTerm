#pragma once

#include "CliPlatform.h"
#include "NotifyUserProtocol.h"

#include <optional>
#include <string>
#include <vector>

// The pure half of `ct`: turning arguments into options and requests, and the
// recording's state file to and from text. Cli.cpp does the I/O around it.
namespace term::cli
{
struct ParsedOptions
{
    std::optional<Options> options;
    std::string error;
};

// Every name in args must be in allowed; all but --interactive and --audio
// take the next argument as their value.
ParsedOptions parseCaptureOptions(const std::vector<std::string>& args,
                                  const std::vector<std::string>& allowed);

struct ParsedNotifyUserRequest
{
    std::optional<NotifyUserRequest> request;
    std::string usageError;
};

// defaultPaneId is $COWTERM_PANE, which --pane overrides.
ParsedNotifyUserRequest parseNotifyUserArguments(const std::vector<std::string>& args,
                                                 const std::string& defaultPaneId);

struct Recording
{
    long long pid = 0;
    std::string path;
    long long started = 0;
};

std::string formatRecordingState(const Recording& recording);

// Reads back only what formatRecordingState writes; not a general JSON parser.
std::optional<Recording> parseRecordingState(const std::string& text);
} // namespace term::cli
