#include "Pty.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <string>

#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <util.h>
#else
#include <pty.h>
#endif

namespace term
{
namespace
{
// shutdown() polls for the reaped shell every shutdownPollMs, and if it has not
// hung up within shutdownKillGraceMs it escalates from SIGHUP to SIGKILL.
constexpr int shutdownPollMs = 10;
constexpr int shutdownKillGraceMs = 250;

[[noreturn]] void execShell(const std::string& workingDirectory,
                            const std::string& command)
{
    // SIGHUP is deliberately included: the daemon sets it to SIG_IGN so the
    // GUI dying can't take it down, and an ignored disposition survives execve.
    // Left alone, the shell would inherit SIG_IGN and never hang up when we
    // close its terminal — wedging quit. Reset it so a hangup ends the shell.
    for (auto sig: {SIGHUP, SIGINT, SIGQUIT, SIGTSTP, SIGPIPE, SIGCHLD})
        signal(sig, SIG_DFL);

    // Everything above the tty belongs to us, not to the user's shell:
    // the web gateway's listening sockets, the daemon's IPC channel, the
    // session database, other panes' PTY masters. Inherited, they outlive
    // us — a shell left holding the gateway's listener keeps the port
    // bound after CowTerm exits, and the next launch cannot serve on it.
    // forkpty has already dup'd the slave onto 0/1/2, so close the rest.
    // _SC_OPEN_MAX follows RLIMIT_NOFILE, which can be enormous; the table
    // is only ever as large as the highest fd we actually opened, so cap
    // the sweep rather than issue millions of pointless close() calls.
    constexpr auto highestPlausibleFd = 4096;
    const auto limit = std::min((int) sysconf(_SC_OPEN_MAX), highestPlausibleFd);

    for (auto descriptor = 3; descriptor < limit; ++descriptor)
        close(descriptor);

    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    unsetenv("TERM_PROGRAM");

    if (getenv("LANG") == nullptr)
        setenv("LANG", "en_US.UTF-8", 1);

    if (workingDirectory.empty() || chdir(workingDirectory.c_str()) != 0)
        if (const auto* home = getenv("HOME"))
            chdir(home);

    const auto* shell = getenv("SHELL");

    if (shell == nullptr || shell[0] == '\0')
        shell = "/bin/zsh";

    // A leading dash asks the shell to start as a login shell, so the user's
    // usual profile and prompt come up.
    auto shellPath = std::string {shell};
    const auto slash = shellPath.find_last_of('/');
    const auto loginName =
        "-" + (slash == std::string::npos ? shellPath : shellPath.substr(slash + 1));

    if (command.empty())
        execl(shell, loginName.c_str(), (char*) nullptr);
    else
        execl(shell, loginName.c_str(), "-c", command.c_str(), (char*) nullptr);

    _exit(127);
}

winsize toWinsize(const PtySize& size)
{
    auto ws = winsize {};
    ws.ws_col = (unsigned short) size.cols;
    ws.ws_row = (unsigned short) size.rows;
    return ws;
}
} // namespace

Pty::~Pty()
{
    shutdown();
}

bool Pty::start(const PtyOptions& options,
                std::function<void(std::string)> onOutput,
                std::function<void()> onExit)
{
    if (fd >= 0)
        return false;

    auto ws = toWinsize(options.size);
    auto masterFd = -1;
    const auto child = forkpty(&masterFd, nullptr, nullptr, &ws);

    if (child < 0)
        return false;

    if (child == 0)
        execShell(options.workingDirectory, options.command);

    fd = masterFd;
    pid = child;

    reader = std::thread(
        [masterFd, output = std::move(onOutput), exit = std::move(onExit)]
        {
            char buffer[65536];

            while (true)
            {
                const auto count = read(masterFd, buffer, sizeof(buffer));

                if (count > 0)
                {
                    output(std::string {buffer, (std::size_t) count});
                    continue;
                }

                if (count < 0 && (errno == EINTR || errno == EAGAIN))
                    continue;

                break;
            }

            exit();
        });

    return true;
}

void Pty::write(std::string_view data)
{
    auto remaining = data;

    while (fd >= 0 && !remaining.empty())
    {
        const auto written = ::write(fd, remaining.data(), remaining.size());

        if (written > 0)
        {
            remaining.remove_prefix((std::size_t) written);
            continue;
        }

        if (written < 0 && (errno == EINTR || errno == EAGAIN))
            continue;

        break;
    }
}

void Pty::resize(const PtySize& size)
{
    if (fd < 0)
        return;

    auto ws = toWinsize(size);
    ioctl(fd, TIOCSWINSZ, &ws);
}

std::string Pty::foregroundProcess() const
{
    if (fd < 0)
        return {};

    const auto pgid = tcgetpgrp(fd);

    if (pgid <= 0)
        return {};

#if defined(__APPLE__)
    char name[2 * MAXCOMLEN] = {};

    if (proc_name(pgid, name, sizeof(name)) > 0)
        return name;

    return {};
#else
    auto comm = std::string {"/proc/"} + std::to_string(pgid) + "/comm";

    if (auto* file = fopen(comm.c_str(), "r"))
    {
        char name[256] = {};
        const auto* result = fgets(name, sizeof(name), file);
        fclose(file);

        if (result != nullptr)
        {
            auto text = std::string {name};

            if (!text.empty() && text.back() == '\n')
                text.pop_back();

            return text;
        }
    }

    return {};
#endif
}

std::string Pty::currentWorkingDirectory() const
{
    if (pid <= 0)
        return {};

#if defined(__APPLE__)
    auto info = proc_vnodepathinfo {};

    if (proc_pidinfo((pid_t) pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof(info)) > 0)
        return info.pvi_cdir.vip_path;

    return {};
#else
    char path[4096] = {};
    const auto link = "/proc/" + std::to_string(pid) + "/cwd";
    const auto count = readlink(link.c_str(), path, sizeof(path) - 1);
    return count > 0 ? std::string {path, (std::size_t) count} : std::string {};
#endif
}

bool Pty::isRunning() const
{
    if (pid < 0)
        return false;

    return ::kill((pid_t) pid, 0) == 0;
}

void Pty::shutdown()
{
    // Order matters, and this used to hang the app on quit. Closing the master
    // fd while the reader thread is still blocked in read() on it deadlocks in
    // the kernel on the tty lock: close() waits for the in-flight read to drop
    // the vnode, but that read only returns once the slave side is gone. So end
    // the shell first — that closes the slave, the reader's read() returns EOF
    // and unwinds — and only then join the reader and close the fd.
    if (pid > 0)
    {
        // Signal the whole foreground process group (forkpty made the child a
        // session leader, so its pid is the group id). SIGHUP alone would leave
        // grandchildren holding the slave open, so the master would never see
        // EOF and the reader would never unwind; the group hangs them all up.
        ::kill((pid_t) -pid, SIGHUP);

        // Reap the shell, escalating to an unignorable SIGKILL if it does not
        // hang up promptly. A shell that traps or ignores SIGHUP must never be
        // able to wedge our own shutdown, so we never block indefinitely here.
        for (int elapsedMs = 0;; elapsedMs += shutdownPollMs)
        {
            if (waitpid((pid_t) pid, nullptr, WNOHANG) != 0)
                break;

            if (elapsedMs == shutdownKillGraceMs)
                ::kill((pid_t) -pid, SIGKILL);

            usleep(shutdownPollMs * 1000);
        }

        pid = -1;
    }

    if (reader.joinable())
        reader.join();

    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}
} // namespace term
