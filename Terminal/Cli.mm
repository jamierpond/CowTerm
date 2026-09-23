// ct: CowTerm's command-line mode. `ct` is a symlink (or alias) to the CowTerm
// executable itself, so the CLI is the same signed binary as the app.
//
// Its job today is screen capture for agents. A process in a CowTerm shell has
// CowTerm as its responsible app, so macOS asks — and remembers — Screen
// Recording on CowTerm's behalf. That memory is keyed on CowTerm's code
// signature, which is why `status` reports it.

#include "Cli.h"
#include "CowTermVersion.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <Foundation/Foundation.h>

#include <fcntl.h>
#include <mach-o/dyld.h>
#include <libproc.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace term::cli
{
namespace
{
constexpr auto screencapture = "/usr/sbin/screencapture";

// Exit codes agents can branch on.
constexpr int exitOk = 0;
constexpr int exitFail = 1;
constexpr int exitNoPermission = 2;

constexpr auto usage = R"(usage: ct <command>

  screen-cap status               granted / not granted, and CowTerm's signature
  screen-cap request              ask macOS for Screen Recording
  screen-cap shot [options]       screenshot; prints the PNG's path
      -o <file>  --display <n>  --window <id>  --rect x,y,w,h  --interactive
  screen-cap record start [options]
      -o <file.mov>  --display <n>  --duration <sec>  --audio
  screen-cap record stop          finish the recording; prints its path
  screen-cap record status

  screenshot                      = screen-cap shot
  start-whole-screen-recording    = screen-cap record start
  stop-screen-recording           = screen-cap record stop
  version

Captures default to $COWTERM_CAPTURE_DIR, else ~/Pictures/CowTerm.
Exit codes: 0 ok, 1 error, 2 Screen Recording not granted,
3 not running inside CowTerm (the grant belongs to the hosting terminal).
)";

int fail(const std::string& message)
{
    std::fprintf(stderr, "ct: %s\n", message.c_str());
    return exitFail;
}

std::string home()
{
    const auto* value = std::getenv("HOME");
    return value != nullptr ? value : "";
}

fs::path executablePath()
{
    auto size = uint32_t {PATH_MAX};
    auto buffer = std::vector<char>(size);

    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        buffer.resize(size);
        _NSGetExecutablePath(buffer.data(), &size);
    }

    auto error = std::error_code {};
    auto resolved = fs::canonical(buffer.data(), error);
    return error ? fs::path {buffer.data()} : resolved;
}

// The .app that contains this executable — through ~/.local/bin/ct's symlink,
// since executablePath() resolves it.
std::optional<fs::path> appBundlePath()
{
    for (auto path = executablePath(); path.has_parent_path() && path != path.root_path();
         path = path.parent_path())
        if (path.extension() == ".app")
            return path;

    return std::nullopt;
}

struct Output
{
    int status = -1;
    std::string text;
};

// Runs a program to completion, stdout and stderr merged into the result.
Output run(const std::vector<std::string>& args)
{
    auto result = Output {};
    int fds[2];

    if (pipe(fds) != 0)
        return result;

    auto actions = posix_spawn_file_actions_t {};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);

    auto argv = std::vector<char*> {};
    for (const auto& arg: args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    auto pid = pid_t {};
    const auto spawned =
        posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (spawned == 0)
    {
        char chunk[4096];
        for (auto n = read(fds[0], chunk, sizeof chunk); n > 0;
             n = read(fds[0], chunk, sizeof chunk))
            result.text.append(chunk, (std::size_t) n);

        auto status = 0;
        waitpid(pid, &status, 0);
        result.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    close(fds[0]);
    return result;
}

// Runs a program with ct's own stdio, for screencapture's interactive mode.
int runAttached(const std::vector<std::string>& args)
{
    auto argv = std::vector<char*> {};
    for (const auto& arg: args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    auto pid = pid_t {};
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
        return -1;

    auto status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string timestamp()
{
    const auto now = std::time(nullptr);
    auto local = std::tm {};
    localtime_r(&now, &local);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y%m%d-%H%M%S", &local);
    return buffer;
}

fs::path captureDir()
{
    const auto* dir = std::getenv("COWTERM_CAPTURE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        return dir;
    return fs::path {home()} / "Pictures" / "CowTerm";
}

// Resolves -o (or the default name) to an absolute path whose directory exists.
std::optional<fs::path> outputPath(const std::string& requested,
                                   const std::string& prefix,
                                   const std::string& extension)
{
    auto path = requested.empty()
                    ? captureDir() / (prefix + "-" + timestamp() + extension)
                    : fs::path {requested};
    path = fs::absolute(path);

    auto error = std::error_code {};
    fs::create_directories(path.parent_path(), error);

    if (error)
    {
        fail("cannot create " + path.parent_path().string() + ": " + error.message());
        return std::nullopt;
    }

    return path;
}

// Without the grant, screencapture fails ("could not create image from
// display") or, on older systems, quietly records only the wallpaper. Either
// way the agent learns nothing useful, so refuse up front.
bool granted()
{
    return CGPreflightScreenCaptureAccess();
}

int notGranted()
{
    std::fprintf(stderr, "ct: Screen Recording not granted; run: ct screen-cap request\n");
    return exitNoPermission;
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

int status()
{
    const auto ok = granted();
    std::printf("%s\n", ok ? "granted" : "not granted");

    const auto app = appBundlePath();
    if (!app)
    {
        std::printf("app: none (not running from a .app bundle)\n");
        return ok ? exitOk : exitFail;
    }

    const auto signature = run({"/usr/bin/codesign", "-dvv", app->string()});
    std::printf("app: %s\n", app->c_str());

    if (signature.status != 0)
    {
        std::printf("signature: unsigned or invalid\n");
    }
    else if (field(signature.text, "Signature") == "adhoc")
    {
        std::printf("signature: adhoc\n");
        std::fflush(stdout);
        std::fprintf(stderr,
                     "ct: WARNING: %s is ad-hoc signed. macOS keys the Screen Recording "
                     "grant on the signature, and an ad-hoc one changes with every "
                     "build, so the grant will not stick. Rebuild with a signing "
                     "identity (`just signing` shows why it went ad-hoc) and "
                     "`just install`.\n",
                     app->c_str());
    }
    else
    {
        const auto authority = field(signature.text, "Authority");
        std::printf("signature: %s\n", authority.empty() ? "signed" : authority.c_str());
    }

    return ok ? exitOk : exitFail;
}

int request()
{
    if (CGRequestScreenCaptureAccess())
    {
        std::printf("granted\n");
        return exitOk;
    }

    std::fprintf(stderr,
                 "ct: Screen Recording not granted. Enable CowTerm in System Settings > "
                 "Privacy & Security > Screen & System Audio Recording (macOS may have "
                 "opened it), then restart CowTerm (Kill everything & quit) — the grant "
                 "only applies to a freshly launched app.\n");
    return exitFail;
}

// Every option after the subcommand takes one value except the listed flags.
struct Options
{
    std::string output;
    std::string display;
    std::string window;
    std::string rect;
    std::string duration;
    bool interactive = false;
    bool audio = false;
};

std::optional<Options> parseOptions(const std::vector<std::string>& args,
                                    const std::vector<std::string>& allowed)
{
    auto options = Options {};
    const auto isAllowed = [&](const std::string& name)
    { return std::find(allowed.begin(), allowed.end(), name) != allowed.end(); };

    for (auto i = std::size_t {0}; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if (!isAllowed(arg))
        {
            fail("unknown option: " + arg + " (see ct help)");
            return std::nullopt;
        }

        if (arg == "--interactive")
        {
            options.interactive = true;
            continue;
        }

        if (arg == "--audio")
        {
            options.audio = true;
            continue;
        }

        if (i + 1 >= args.size())
        {
            fail(arg + " needs a value");
            return std::nullopt;
        }

        const auto& value = args[++i];

        if (arg == "-o")
            options.output = value;
        else if (arg == "--display")
            options.display = value;
        else if (arg == "--window")
            options.window = value;
        else if (arg == "--rect")
            options.rect = value;
        else if (arg == "--duration")
            options.duration = value;
    }

    return options;
}

int shot(const std::vector<std::string>& args)
{
    const auto options = parseOptions(
        args, {"-o", "--display", "--window", "--rect", "--interactive"});
    if (!options)
        return exitFail;

    if (!granted())
        return notGranted();

    const auto path = outputPath(options->output, "shot", ".png");
    if (!path)
        return exitFail;

    auto command = std::vector<std::string> {screencapture, "-x", "-t", "png"};

    if (!options->display.empty())
        command.insert(command.end(), {"-D", options->display});
    if (!options->window.empty())
        command.insert(command.end(), {"-l", options->window});
    if (!options->rect.empty())
        command.insert(command.end(), {"-R", options->rect});
    if (options->interactive)
        command.push_back("-i");

    command.push_back(path->string());

    auto code = 0;
    auto message = std::string {};

    if (options->interactive)
    {
        code = runAttached(command);
    }
    else
    {
        const auto result = run(command);
        code = result.status;
        message = result.text;
    }

    // An interactive capture cancelled with Escape exits 0 and writes nothing.
    if (code != 0 || !fs::exists(*path))
    {
        while (!message.empty() && message.back() == '\n')
            message.pop_back();
        return fail(message.empty() ? "screencapture produced no image"
                                    : "screencapture: " + message);
    }

    std::printf("%s\n", path->c_str());
    return exitOk;
}

// --- recording ---------------------------------------------------------------

struct Recording
{
    pid_t pid = 0;
    std::string path;
    long long started = 0;
};

fs::path stateFile()
{
    // A named instance ($COWTERM_INSTANCE, see Terminal/Protocol.h) keeps its
    // own state, as it does its config.
    const auto* instance = std::getenv("COWTERM_INSTANCE");
    const auto name = instance != nullptr && instance[0] != '\0'
                          ? "screen-cap." + std::string {instance} + ".json"
                          : std::string {"screen-cap.json"};
    return fs::path {home()} / "Library" / "Application Support" / "CowTerm" / name;
}

std::string jsonEscape(const std::string& text)
{
    auto out = std::string {};
    for (auto c: text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

// Reads back only what writeState() writes; not a general JSON parser.
std::optional<Recording> readState()
{
    auto file = std::ifstream {stateFile()};
    if (!file)
        return std::nullopt;

    const auto text = std::string {std::istreambuf_iterator<char> {file}, {}};
    const auto number = [&](const std::string& key) -> long long
    {
        const auto at = text.find("\"" + key + "\":");
        return at == std::string::npos ? 0 : std::atoll(text.c_str() + at + key.size() + 3);
    };

    auto recording = Recording {};
    recording.pid = (pid_t) number("pid");
    recording.started = number("started");

    const auto at = text.find("\"path\":\"");
    if (at != std::string::npos)
    {
        for (auto i = at + 8; i < text.size() && text[i] != '"'; ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
                ++i;
            recording.path += text[i];
        }
    }

    if (recording.pid <= 0 || recording.path.empty())
        return std::nullopt;

    return recording;
}

bool writeState(const Recording& recording)
{
    auto error = std::error_code {};
    fs::create_directories(stateFile().parent_path(), error);

    auto file = std::ofstream {stateFile()};
    file << "{\"pid\":" << recording.pid << ",\"path\":\"" << jsonEscape(recording.path)
         << "\",\"started\":" << recording.started << "}\n";
    return (bool) file;
}

void clearState()
{
    auto error = std::error_code {};
    fs::remove(stateFile(), error);
}

// The pid alone could have been recycled since the state file was written.
bool isScreencapture(pid_t pid)
{
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(pid, path, sizeof path) <= 0)
        return false;
    return fs::path {path}.filename() == "screencapture";
}

std::optional<Recording> activeRecording()
{
    auto recording = readState();

    if (recording && !isScreencapture(recording->pid))
    {
        clearState();
        return std::nullopt;
    }

    return recording;
}

long long now()
{
    return (long long) std::time(nullptr);
}

int recordStart(const std::vector<std::string>& args)
{
    const auto options =
        parseOptions(args, {"-o", "--display", "--duration", "--audio"});
    if (!options)
        return exitFail;

    if (const auto active = activeRecording())
    {
        std::fprintf(stderr, "ct: already recording to %s (ct screen-cap record stop)\n",
                     active->path.c_str());
        return exitFail;
    }

    if (!granted())
        return notGranted();

    const auto path = outputPath(options->output, "recording", ".mov");
    if (!path)
        return exitFail;

    auto command = std::vector<std::string> {screencapture, "-v"};

    if (!options->duration.empty())
        command.insert(command.end(), {"-V", options->duration});
    if (options->audio)
        command.push_back("-g");
    if (!options->display.empty())
        command.insert(command.end(), {"-D", options->display});

    command.insert(command.end(), {"-x", path->string()});

    auto argv = std::vector<char*> {};
    for (const auto& arg: command)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    // screencapture -v stops on any keypress from stdin, so stdin must never
    // deliver data or EOF. The child gets the read end of a pipe and also holds
    // the write end itself (fd 3): it outlives ct and never sees end-of-file.
    int fds[2];
    if (pipe(fds) != 0)
        return fail("pipe failed");

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
        return fail(std::string {"cannot start screencapture: "} + std::strerror(spawned));

    // A bad display number or a refused capture ends it at once; report that
    // rather than a recording that isn't happening.
    std::this_thread::sleep_for(std::chrono::milliseconds {500});
    auto status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid)
        return fail("screencapture exited immediately (status "
                    + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1)
                    + ")");

    if (!writeState({pid, path->string(), now()}))
        return fail("cannot write " + stateFile().string());

    std::printf("%s\n", path->c_str());
    return exitOk;
}

int recordStop()
{
    const auto active = activeRecording();
    if (!active)
        return fail("nothing is recording");

    // screencapture finalises the movie on SIGINT, as it would on Ctrl+C.
    kill(active->pid, SIGINT);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {10};
    while (isScreencapture(active->pid) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds {100});

    while (!fs::exists(active->path) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds {100});

    if (isScreencapture(active->pid))
        return fail("screencapture (pid " + std::to_string(active->pid)
                    + ") did not exit within 10s; recording state kept");

    clearState();

    if (!fs::exists(active->path))
        return fail("recording stopped but no file was written: " + active->path);

    std::printf("%s\n", active->path.c_str());
    return exitOk;
}

int recordStatus()
{
    const auto active = activeRecording();
    if (!active)
    {
        std::printf("not recording\n");
        return exitOk;
    }

    const auto elapsed = now() - active->started;
    std::printf("recording\npath: %s\nelapsed: %lld:%02lld\n", active->path.c_str(),
                elapsed / 60, elapsed % 60);
    return exitOk;
}

int record(const std::vector<std::string>& args)
{
    if (args.empty())
        return fail("record needs start, stop or status (see ct help)");

    const auto rest = std::vector<std::string>(args.begin() + 1, args.end());

    if (args[0] == "start")
        return recordStart(rest);
    if (args[0] == "stop")
        return recordStop();
    if (args[0] == "status")
        return recordStatus();

    return fail("unknown record command: " + args[0]);
}

int screenCap(const std::vector<std::string>& args)
{
    if (args.empty())
        return fail("screen-cap needs a command (see ct help)");

    const auto rest = std::vector<std::string>(args.begin() + 1, args.end());

    if (args[0] == "status")
        return status();
    if (args[0] == "request")
        return request();
    if (args[0] == "shot")
        return shot(rest);
    if (args[0] == "record")
        return record(rest);

    return fail("unknown screen-cap command: " + args[0]);
}
// macOS attributes the Screen Recording request to the terminal hosting the
// shell. Run from iTerm or over ssh, ct would prompt for (and get or be refused
// by) the wrong app, so the capture commands only run under CowTerm: some
// ancestor must be its app or its daemon (which owns every CowTerm PTY).
bool insideCowTerm(std::string& parent)
{
    const auto hostSuffixes = {"/CowTerm.app/Contents/MacOS/CowTerm",
                               "/CowTerm.app/Contents/MacOS/CowTermDaemon"};

    for (auto pid = getppid(); pid > 1;)
    {
        char path[PROC_PIDPATHINFO_MAXSIZE];
        const auto executable =
            proc_pidpath(pid, path, sizeof path) > 0 ? std::string {path} : std::string {};

        if (parent.empty())
            parent = executable.empty() ? "pid " + std::to_string(pid) : executable;

        for (const auto* suffix: hostSuffixes)
            if (executable.size() >= std::strlen(suffix)
                && executable.compare(executable.size() - std::strlen(suffix),
                                      std::string::npos,
                                      suffix)
                       == 0)
                return true;

        auto info = proc_bsdinfo {};
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof info) != sizeof info)
            break;

        pid = (pid_t) info.pbi_ppid;
    }

    if (parent.empty())
        parent = "launchd";

    return false;
}

constexpr int exitNotInCowTerm = 3;

int dispatch(const std::vector<std::string>& args)
{
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h")
    {
        std::fputs(usage, stdout);
        return exitOk;
    }

    if (args[0] == "version" || args[0] == "--version")
    {
        std::printf("ct %s (%s)\n", appVersion, versionTag().c_str());
        return exitOk;
    }

    auto parent = std::string {};
    if (!insideCowTerm(parent))
    {
        std::fprintf(stderr,
                     "ct: not running inside CowTerm (parent is %s); Screen Recording "
                     "is granted to the hosting terminal, so run this from a CowTerm "
                     "shell\n",
                     parent.c_str());
        return exitNotInCowTerm;
    }

    const auto rest = std::vector<std::string>(args.begin() + 1, args.end());
    const auto prepend = [&](std::vector<std::string> head)
    {
        head.insert(head.end(), rest.begin(), rest.end());
        return head;
    };

    if (args[0] == "screen-cap")
        return screenCap(rest);
    if (args[0] == "screenshot")
        return screenCap(prepend({"shot"}));
    if (args[0] == "start-whole-screen-recording")
        return screenCap(prepend({"record", "start"}));
    if (args[0] == "stop-screen-recording")
        return screenCap(prepend({"record", "stop"}));

    return fail("unknown command: " + args[0] + " (see ct help)");
}
} // namespace
} // namespace term::cli

namespace term
{
bool isCliInvocation(int argc, char** argv)
{
    if (argc > 0 && fs::path {argv[0]}.filename() == "ct")
        return true;

    if (argc < 2)
        return false;

    const auto verb = std::string {argv[1]};
    for (const auto* known: {"screen-cap",
                             "screenshot",
                             "start-whole-screen-recording",
                             "stop-screen-recording",
                             "help",
                             "--help",
                             "-h",
                             "version",
                             "--version"})
        if (verb == known)
            return true;

    return false;
}

int runCli(int argc, char** argv)
{
    @autoreleasepool
    {
        return cli::dispatch(std::vector<std::string>(argv + 1, argv + argc));
    }
}
} // namespace term
