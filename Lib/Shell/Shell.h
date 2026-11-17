#pragma once

#include "Pty.h"

#include <functional>
#include <optional>
#include <string>

namespace term
{
// What a pane needs from the process behind it, regardless of where that
// process lives: in this app (LocalShell wraps a Pty) or held by the
// session daemon so it survives the GUI (RemoteShell in DaemonClient.cpp).
//
// Output may arrive on any thread (local: the PTY reader thread; remote:
// the main thread) — receivers marshal for themselves. terminate() ends the
// process on purpose (a closed pane); detach() just stops listening, which
// for a daemon shell means it keeps running.
class Shell
{
public:
    virtual ~Shell() = default;

    virtual bool start(const PtyOptions& options,
                       std::function<void(std::string)> onOutput,
                       std::function<void()> onExit) = 0;

    virtual void write(std::string_view data) = 0;
    virtual void resize(const PtySize& size) = 0;

    // Bytes the user typed or pasted, as against the terminal's own replies.
    // A daemon shell tells the daemon, which remembers the pane last typed into.
    virtual void writeUserInput(std::string_view data) { write(data); }

    virtual std::string foregroundProcess() const = 0;
    virtual std::string currentWorkingDirectory() const = 0;

    virtual void terminate() = 0;
    virtual void detach() = 0;

    // Set before start(). A shell held by the session daemon hands back the
    // serialized TerminalState it kept, before any output; local shells
    // never call it.
    std::function<void(const std::string&)> onRestoreState =
        [](const std::string&) {};

    // Set before start(). A short note about the shell's connection for the
    // pane to show over its text ("tamby: connection lost; reconnecting…"),
    // or empty to clear it; local shells never call it.
    std::function<void(const std::string&)> onStatusChanged =
        [](const std::string&) {};

    // Set before start(). Another client holds the pane's size (a phone
    // showing it): the pane is drawn at that grid until it is empty again.
    std::function<void(std::optional<PtySize>)> onGridSetElsewhere = [](std::optional<PtySize>) {};

    // Set before start(). A command pane's program is not installed where it
    // would run, named here; the pane's text says so too. Local shells never
    // call it.
    std::function<void(const std::string& program)> onCommandNotInstalled = [](const std::string&) {};

    // The user asked for the pane back from whoever holds its size (a click
    // on the note that says so). Nothing for a shell no one else can hold.
    virtual void takeSizeBack() {}
};

// In-process shell: the pane owns the PTY directly. detach() still kills —
// a local shell cannot outlive its owner.
class LocalShell final : public Shell
{
public:
    bool start(const PtyOptions& options,
               std::function<void(std::string)> onOutput,
               std::function<void()> onExit) override
    {
        return pty.start(options, std::move(onOutput), std::move(onExit));
    }

    void write(std::string_view data) override { pty.write(data); }
    void resize(const PtySize& size) override { pty.resize(size); }

    std::string foregroundProcess() const override
    {
        return pty.foregroundProcess();
    }

    std::string currentWorkingDirectory() const override
    {
        return pty.currentWorkingDirectory();
    }

    void terminate() override { pty.shutdown(); }
    void detach() override { pty.shutdown(); }

private:
    Pty pty;
};
} // namespace term
