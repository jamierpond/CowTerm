// ct on macOS: screencapture for the captures, TCC for the grant, libproc for
// the process tree. The commands themselves are Cli.cpp.

#include "CliPlatform.h"
#include "MacProcesses.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <Foundation/Foundation.h>

#include <fcntl.h>
#include <libproc.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace term::cli::platform
{
namespace
{
constexpr auto screencapture = "/usr/sbin/screencapture";

// Runs a program to completion, stdout and stderr merged into the result.
Capture run(const std::vector<std::string>& args)
{
    auto output = macos::runProcessCapturingOutput(args, macos::StandardInput::inherited);
    return Capture {output.status, std::move(output.text)};
}

// Runs a program with ct's own stdio, for screencapture's interactive mode.
int runAttached(const std::vector<std::string>& args)
{
    auto argv = macos::createArgumentVector(args);

    auto pid = pid_t {};
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
        return -1;

    auto status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Pulls "Key=value" out of `codesign -dvv` output.
std::string field(const std::string& text, const std::string& key)
{
    auto stream = std::istringstream {text};
    for (auto line = std::string {}; std::getline(stream, line);)
        if (line.rfind(key + "=", 0) == 0)
            return line.substr(key.size() + 1);
    return {};
}
} // namespace

// A terminal launch already hands ct its stdio.
void attachToTerminal() {}

std::string timestamp()
{
    const auto now = std::time(nullptr);
    auto local = std::tm {};
    localtime_r(&now, &local);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y%m%d-%H%M%S", &local);
    return buffer;
}

bool captureGranted()
{
    return CGPreflightScreenCaptureAccess();
}

bool requestCapture()
{
    if (CGRequestScreenCaptureAccess())
    {
        std::printf("granted\n");
        return true;
    }

    std::fprintf(stderr,
                 "ct: Screen Recording not granted. Enable CowTerm in System Settings > "
                 "Privacy & Security > Screen & System Audio Recording (macOS may have "
                 "opened it), then restart CowTerm (Kill everything & quit) — the grant "
                 "only applies to a freshly launched app.\n");
    return false;
}

void printGrantDetails()
{
    const auto app = macos::findAppBundlePath();
    if (!app)
    {
        std::printf("app: none (not running from a .app bundle)\n");
        return;
    }

    const auto signature = run({"/usr/bin/codesign", "-dvv", app->string()});
    std::printf("app: %s\n", app->c_str());

    if (signature.status != 0)
    {
        std::printf("signature: unsigned or invalid\n");
    }
    else if (field(signature.message, "Signature") == "adhoc")
    {
        std::printf("signature: adhoc\n");
        std::fflush(stdout);
        std::fprintf(stderr,
                     "ct: note: %s is ad-hoc signed. macOS keys the Screen Recording "
                     "grant on the signature, and an ad-hoc one changes with every "
                     "build, so the grant has to be given again after a rebuild.\n",
                     app->c_str());
    }
    else
    {
        const auto authority = field(signature.message, "Authority");
        std::printf("signature: %s\n", authority.empty() ? "signed" : authority.c_str());
    }
}

Capture screenshot(const Options& options, const fs::path& path)
{
    auto command = std::vector<std::string> {screencapture, "-x", "-t", "png"};

    if (!options.display.empty())
        command.insert(command.end(), {"-D", options.display});
    if (!options.window.empty())
        command.insert(command.end(), {"-l", options.window});
    if (!options.rect.empty())
        command.insert(command.end(), {"-R", options.rect});
    if (options.interactive)
        command.push_back("-i");

    command.push_back(path.string());

    if (options.interactive)
        return {runAttached(command), {}};

    return run(command);
}

namespace
{
std::vector<std::string> createRecordingCommand(const Options& options, const fs::path& path)
{
    auto command = std::vector<std::string> {screencapture, "-v"};

    if (!options.duration.empty())
        command.insert(command.end(), {"-V", options.duration});
    if (options.audio)
        command.push_back("-g");
    if (!options.display.empty())
        command.insert(command.end(), {"-D", options.display});

    command.insert(command.end(), {"-x", path.string()});
    return command;
}

// A bad display number or a refused capture ends it at once; report that
// rather than a recording that isn't happening.
std::optional<int> findEarlyRecorderExit(pid_t pid)
{
    constexpr auto recorderStartupGrace = std::chrono::milliseconds {500};
    std::this_thread::sleep_for(recorderStartupGrace);

    auto status = 0;
    if (waitpid(pid, &status, WNOHANG) != pid)
        return std::nullopt;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return -1;
}
} // namespace

std::optional<long long> startRecording(const Options& options,
                                        const fs::path& path,
                                        std::string& error)
{
    const auto command = createRecordingCommand(options, path);
    auto argv = macos::createArgumentVector(command);

    // screencapture -v stops on any keypress from stdin, so stdin must never
    // deliver data or EOF. The child gets the read end of a pipe and also holds
    // the write end itself (fd 3): it outlives ct and never sees end-of-file.
    int fds[2];
    if (pipe(fds) != 0)
    {
        error = "pipe failed";
        return std::nullopt;
    }

    auto actions = posix_spawn_file_actions_t {};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 3);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    // Its own session, so closing the CowTerm pane (SIGHUP to the foreground
    // group) doesn't cut the recording short. CLOEXEC_DEFAULT closes every fd
    // not named above.
    auto attributes = posix_spawnattr_t {};
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT);

    auto pid = pid_t {};
    const auto spawned =
        posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(fds[0]);
    close(fds[1]);

    if (spawned != 0)
    {
        error = std::string {"cannot start screencapture: "} + std::strerror(spawned);
        return std::nullopt;
    }

    if (const auto exitStatus = findEarlyRecorderExit(pid))
    {
        error = "screencapture exited immediately (status " + std::to_string(*exitStatus) + ")";
        return std::nullopt;
    }

    return (long long) pid;
}

bool isRecorder(long long pid)
{
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath((pid_t) pid, path, sizeof path) <= 0)
        return false;
    return fs::path {path}.filename() == "screencapture";
}

// screencapture finalises the movie on SIGINT, as it would on Ctrl+C.
void stopRecording(long long pid)
{
    kill((pid_t) pid, SIGINT);
}

namespace
{
std::string findProcessExecutable(pid_t pid)
{
    char path[PROC_PIDPATHINFO_MAXSIZE];

    if (proc_pidpath(pid, path, sizeof path) > 0)
        return path;

    return {};
}

bool isCowTermHost(const std::string& executable)
{
    constexpr auto hostSuffixes = std::array {
        std::string_view {"/CowTerm.app/Contents/MacOS/CowTerm"},
        std::string_view {"/CowTerm.app/Contents/MacOS/CowTermDaemon"},
    };

    return std::ranges::any_of(hostSuffixes,
                               [&](std::string_view suffix) { return executable.ends_with(suffix); });
}

std::optional<pid_t> findParentProcess(pid_t pid)
{
    auto info = proc_bsdinfo {};

    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof info) != sizeof info)
        return std::nullopt;

    return static_cast<pid_t>(info.pbi_ppid);
}

std::string describeProcess(pid_t pid, const std::string& executable)
{
    if (executable.empty())
        return "pid " + std::to_string(pid);

    return executable;
}
} // namespace

// Some ancestor must be the app or its daemon (which owns every CowTerm PTY).
bool insideCowTerm(std::string& parent)
{
    for (auto pid = getppid(); pid > 1;)
    {
        const auto executable = findProcessExecutable(pid);

        if (parent.empty())
            parent = describeProcess(pid, executable);

        if (isCowTermHost(executable))
            return true;

        const auto parentProcess = findParentProcess(pid);

        if (!parentProcess)
            break;

        pid = *parentProcess;
    }

    if (parent.empty())
        parent = "launchd";

    return false;
}
} // namespace term::cli::platform
