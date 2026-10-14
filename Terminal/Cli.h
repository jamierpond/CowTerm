#pragma once

// The CowTerm executable doubles as the `ct` command-line tool: `ct` is a
// symlink or alias to it (ct.cmd on Windows). See Cli.cpp.
namespace term
{
// True when invoked as `ct`, or with a CLI verb as the first argument. A
// Finder / `open` launch passes neither, so the GUI path is untouched.
bool isCliInvocation(int argc, char** argv);

int runCli(int argc, char** argv);
} // namespace term
