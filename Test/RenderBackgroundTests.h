#pragma once

// The commands the render tests run, in the shell the platform's Pty runs
// commands through: POSIX sh (RenderBackgroundTests-Posix.cpp) or PowerShell
// (RenderBackgroundTests-Windows.cpp). Each prints HELLO without a newline.
namespace term::tests
{
// HELLO and five blank cells, all on ANSI red (SGR 41), then a reset.
extern const char* const redHelloCommand;

// HELLO with no styling at all.
extern const char* const plainHelloCommand;
} // namespace term::tests
