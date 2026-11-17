#include "Pty.h"

#include <cassert>

// iOS forbids third-party apps from starting processes (fork, forkpty and exec
// fail with EPERM), so a pane on the phone never has a local shell; every pane
// there is a DaemonShell. Like eacp's Process-iOS.cpp, start() asserts in debug
// and otherwise reports that nothing started.
namespace term
{
struct Pty::Native
{
};

Pty::Pty()
    : native(std::make_unique<Native>())
{
}

Pty::~Pty() = default;

bool Pty::start(const PtyOptions&,
                std::function<void(std::string)>,
                std::function<void()>)
{
    assert(false && "A local shell is unavailable on iOS");
    return false;
}

void Pty::write(std::string_view)
{
}

void Pty::resize(const PtySize&)
{
}

bool Pty::isRunning() const
{
    return false;
}

void Pty::shutdown()
{
}

std::string Pty::foregroundProcess() const
{
    return {};
}

std::string Pty::currentWorkingDirectory() const
{
    return {};
}
} // namespace term
