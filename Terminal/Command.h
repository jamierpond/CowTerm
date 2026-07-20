#pragma once

#include <functional>
#include <string>

namespace term
{
struct CommandOutput
{
    int status = -1;
    std::string text;
};

// Single-quote for POSIX sh, escaping any embedded quote as '\''. Safe to
// nest: the result is a literal to one shell, which is exactly what the
// login-shell wrap below relies on.
std::string shellQuote(const std::string& text);

// Runs a shell command through the user's login shell — a GUI launch carries
// a stripped PATH, so the user's git/gh would otherwise be missing — and
// captures its stdout. Synchronous: call it on a user action or a worker
// thread, never in a render loop. On Windows there is no login shell to
// wrap, so the command runs directly.
CommandOutput runViaLoginShell(const std::string& command);

// runViaLoginShell on a detached worker thread; `done` is marshalled back to
// the main thread. The callee may be long gone by then, so `done` must guard
// its own captures (weak flag), not reference the caller directly.
void runViaLoginShellAsync(std::string command,
                           std::function<void(CommandOutput)> done);
} // namespace term
