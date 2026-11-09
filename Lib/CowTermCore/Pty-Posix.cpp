#include "Pty.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

#include <pwd.h>
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
constexpr int microsecondsPerMillisecond = 1000;
constexpr int execFailedExitCode = 127;
constexpr std::size_t readBufferSize = 65536;

bool isNonEmpty(const char* text)
{
    return text != nullptr && text[0] != '\0';
}

// A read or write that was interrupted or would block is tried again.
bool isRetryableFailure(long result)
{
    return result < 0 && (errno == EINTR || errno == EAGAIN);
}

void setChildEnvironment(const std::vector<EnvironmentVariable>& environment)
{
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    unsetenv("TERM_PROGRAM");

    for (const auto& variable: environment)
        setenv(variable.name.c_str(), variable.value.c_str(), 1);

    if (getenv("LANG") == nullptr)
        setenv("LANG", "en_US.UTF-8", 1);
}

void changeToWorkingDirectory(const std::string& workingDirectory)
{
    if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) == 0)
        return;

    if (const auto* home = getenv("HOME"))
        chdir(home);
}

// A leading dash asks the shell to start as a login shell, so the user's
// usual profile and prompt come up.
std::string loginArgumentName(const std::string& shell)
{
    const auto slash = shell.find_last_of('/');

    if (slash == std::string::npos)
        return "-" + shell;

    return "-" + shell.substr(slash + 1);
}

// The shell arrives resolved rather than being looked up here: this runs in the
// forked child of a threaded process, where getpwuid -- which takes a lock and
// allocates -- can deadlock if another thread held that lock at fork time.
[[noreturn]] void execShell(const std::string& shell,
                            const std::string& workingDirectory,
                            const std::string& command,
                            const std::vector<EnvironmentVariable>& environment)
{
    // SIGHUP is deliberately included: the daemon sets it to SIG_IGN so the
    // GUI dying can't take it down, and an ignored disposition survives execve.
    // Left alone, the shell would inherit SIG_IGN and never hang up when we
    // close its terminal — wedging quit. Reset it so a hangup ends the shell.
    for (auto sig: {SIGHUP, SIGINT, SIGQUIT, SIGTSTP, SIGPIPE, SIGCHLD})
        signal(sig, SIG_DFL);

    setChildEnvironment(environment);
    changeToWorkingDirectory(workingDirectory);

    // So anything the shell starts agrees with the shell actually running,
    // even when the app was launched without SHELL set. Never overwrites a
    // value the session already chose.
    setenv("SHELL", shell.c_str(), 0);

    const auto loginName = loginArgumentName(shell);

    if (command.empty())
        execl(shell.c_str(), loginName.c_str(), (char*) nullptr);
    else
        execl(shell.c_str(),
              loginName.c_str(),
              "-c",
              command.c_str(),
              (char*) nullptr);

    _exit(execFailedExitCode);
}

// Which shell a session runs. SHELL is what a login session sets and what the
// user means, but it is not always there -- a desktop entry, a service manager
// or a bare `env -i` launch can all arrive without it, and then the account's
// own shell from the passwd database is the next best answer.
std::string loginShell()
{
    if (const auto* shell = getenv("SHELL"); isNonEmpty(shell))
        return shell;

    if (const auto* pw = getpwuid(getuid()); pw != nullptr && isNonEmpty(pw->pw_shell))
        return pw->pw_shell;

    // Last resort, and the reason this function exists: zsh is the default
    // shell on macOS but a stock Ubuntu has no /bin/zsh at all, so hardcoding
    // it made execl fail, the session die the instant it opened, and the app
    // quit with "last session closed" before drawing a prompt. Only /bin/sh is
    // guaranteed to be there.
#if defined(__APPLE__)
    return "/bin/zsh";
#else
    return "/bin/sh";
#endif
}

winsize toWinsize(const PtySize& size)
{
    auto ws = winsize {};
    ws.ws_col = (unsigned short) size.cols;
    ws.ws_row = (unsigned short) size.rows;
    return ws;
}

void readUntilHangup(int fd, const std::function<void(std::string)>& output)
{
    char buffer[readBufferSize];

    while (true)
    {
        const auto count = read(fd, buffer, sizeof(buffer));

        if (count > 0)
            output(std::string {buffer, (std::size_t) count});
        else if (!isRetryableFailure(count))
            return;
    }
}

#if !defined(__APPLE__)
std::string readFirstLine(const std::string& path)
{
    constexpr auto lineCapacity = 256;
    auto* file = fopen(path.c_str(), "r");

    if (file == nullptr)
        return {};

    char line[lineCapacity] = {};
    const auto* result = fgets(line, sizeof(line), file);
    fclose(file);

    if (result == nullptr)
        return {};

    auto text = std::string {line};

    if (!text.empty() && text.back() == '\n')
        text.pop_back();

    return text;
}
#endif
} // namespace

struct Pty::Native
{
    int fd = -1;
    long pid = -1;
    std::thread reader;
};

Pty::Pty()
    : native(std::make_unique<Native>())
{
}

Pty::~Pty()
{
    shutdown();
}

bool Pty::start(const PtyOptions& options,
                std::function<void(std::string)> onOutput,
                std::function<void()> onExit)
{
    if (native->fd >= 0)
        return false;

    auto ws = toWinsize(options.size);
    auto masterFd = -1;

    // Before the fork on purpose; see execShell.
    const auto shell = loginShell();
    const auto child = forkpty(&masterFd, nullptr, nullptr, &ws);

    if (child < 0)
        return false;

    if (child == 0)
        execShell(
            shell, options.workingDirectory, options.command, options.environment);

    native->fd = masterFd;
    native->pid = child;

    native->reader = std::thread(
        [masterFd, output = std::move(onOutput), exit = std::move(onExit)]
        {
            readUntilHangup(masterFd, output);
            exit();
        });

    return true;
}

void Pty::write(std::string_view data)
{
    auto remaining = data;

    while (native->fd >= 0 && !remaining.empty())
    {
        const auto written = ::write(native->fd, remaining.data(), remaining.size());

        if (written > 0)
            remaining.remove_prefix((std::size_t) written);
        else if (!isRetryableFailure(written))
            break;
    }
}

void Pty::resize(const PtySize& size)
{
    if (native->fd < 0)
        return;

    auto ws = toWinsize(size);
    ioctl(native->fd, TIOCSWINSZ, &ws);
}

std::string Pty::foregroundProcess() const
{
    if (native->fd < 0)
        return {};

    const auto pgid = tcgetpgrp(native->fd);

    if (pgid <= 0)
        return {};

#if defined(__APPLE__)
    char name[2 * MAXCOMLEN] = {};

    if (proc_name(pgid, name, sizeof(name)) > 0)
        return name;

    return {};
#else
    return readFirstLine("/proc/" + std::to_string(pgid) + "/comm");
#endif
}

std::string Pty::currentWorkingDirectory() const
{
    if (native->pid <= 0)
        return {};

#if defined(__APPLE__)
    auto info = proc_vnodepathinfo {};

    if (proc_pidinfo(
            (pid_t) native->pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof(info))
        > 0)
        return info.pvi_cdir.vip_path;

    return {};
#else
    char path[4096] = {};
    const auto link = "/proc/" + std::to_string(native->pid) + "/cwd";
    const auto count = readlink(link.c_str(), path, sizeof(path) - 1);
    return count > 0 ? std::string {path, (std::size_t) count} : std::string {};
#endif
}

bool Pty::isRunning() const
{
    if (native->pid < 0)
        return false;

    return ::kill((pid_t) native->pid, 0) == 0;
}

void Pty::shutdown()
{
    // Order matters, and this used to hang the app on quit. Closing the master
    // fd while the reader thread is still blocked in read() on it deadlocks in
    // the kernel on the tty lock: close() waits for the in-flight read to drop
    // the vnode, but that read only returns once the slave side is gone. So end
    // the shell first — that closes the slave, the reader's read() returns EOF
    // and unwinds — and only then join the reader and close the fd.
    if (native->pid > 0)
    {
        // Signal the whole foreground process group (forkpty made the child a
        // session leader, so its pid is the group id). SIGHUP alone would leave
        // grandchildren holding the slave open, so the master would never see
        // EOF and the reader would never unwind; the group hangs them all up.
        ::kill((pid_t) -native->pid, SIGHUP);

        // Reap the shell, escalating to an unignorable SIGKILL if it does not
        // hang up promptly. A shell that traps or ignores SIGHUP must never be
        // able to wedge our own shutdown, so we never block indefinitely here.
        for (int elapsedMs = 0;; elapsedMs += shutdownPollMs)
        {
            if (waitpid((pid_t) native->pid, nullptr, WNOHANG) != 0)
                break;

            if (elapsedMs == shutdownKillGraceMs)
                ::kill((pid_t) -native->pid, SIGKILL);

            usleep(shutdownPollMs * microsecondsPerMillisecond);
        }

        native->pid = -1;
    }

    if (native->reader.joinable())
        native->reader.join();

    if (native->fd >= 0)
    {
        close(native->fd);
        native->fd = -1;
    }
}
} // namespace term
