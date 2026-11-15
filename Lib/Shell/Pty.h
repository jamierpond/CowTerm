#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
struct PtySize
{
    int cols = 80;
    int rows = 24;
};

struct EnvironmentVariable
{
    std::string name;
    std::string value;
};

struct PtyOptions
{
    PtySize size;

    // Where the shell starts; empty means the user's home directory.
    std::string workingDirectory;

    // Run this command (through the shell, so the login environment and
    // PATH apply) instead of an interactive shell; the PTY ends when it
    // exits. Empty runs the user's shell.
    std::string command;

    // Set in the shell's environment on top of the one it inherits.
    std::vector<EnvironmentVariable> environment;
};

// A pseudo-terminal running the user's login shell. Output is delivered on a
// background reader thread via onOutput — marshal to the UI thread before
// touching any view state. onExit fires (also on the reader thread) when the
// shell terminates.
class Pty
{
public:
    Pty();
    ~Pty();

    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    bool start(const PtyOptions& options,
               std::function<void(std::string)> onOutput,
               std::function<void()> onExit);

    void write(std::string_view data);
    void resize(const PtySize& size);
    bool isRunning() const;
    void shutdown();

    // Name of the process group currently owning the terminal (what the user
    // sees running: zsh, claude, nvim, ...). Empty when unknown.
    std::string foregroundProcess() const;

    // The shell's live working directory, read from the kernel — no shell
    // integration needed. Empty when unknown.
    std::string currentWorkingDirectory() const;

private:
    // What the platform holds: the master fd, the shell's pid and the reader
    // thread on POSIX (Pty-Posix.cpp); the pseudoconsole, its pipes, the
    // process and its waiter on Windows (Pty-Windows.cpp).
    struct Native;
    std::unique_ptr<Native> native;
};
} // namespace term
