#pragma once

#include "TerminalView/TerminalView.h"

#include <string>

// The shell the phone shows when no host was named at launch, for the
// simulator and its screenshots.
namespace cowterm::mobile
{
struct SampleShellOptions
{
    bool streamsOutput = false;

    // The note a daemon pane shows over its text; empty for none.
    std::string statusNote;

    bool showsClaudePrompt = false;
};

term::TerminalConfig makeSampleShellConfig(const SampleShellOptions& options);
} // namespace cowterm::mobile
