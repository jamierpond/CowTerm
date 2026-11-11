#pragma once

#include "Pty.h"

#include <chrono>
#include <memory>
#include <string>

// The pieces PtyTests.cpp and the platform cases (PtyTests-Posix.cpp,
// PtyTests-Windows.cpp) share.
namespace term::tests
{
constexpr auto budget = std::chrono::milliseconds {2000};

// A command that does nothing, quietly, for a few seconds, in the shell the
// platform's Pty runs commands through.
extern const char* const idleCommand;

// Starts a shell running `command` and lets its reader thread settle into a
// blocking read.
std::unique_ptr<Pty> startShell(const std::string& command);

// Runs pty->shutdown() but never lets a regression hang the whole suite.
bool shutsDownWithin(std::unique_ptr<Pty> pty, std::chrono::milliseconds budget);
} // namespace term::tests
