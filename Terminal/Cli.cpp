// ct: CowTerm's command-line mode. `ct` is a symlink (or alias) to the CowTerm
// executable itself, so the CLI is the same signed binary as the app.
//
// Its job today is screen capture for agents. On macOS a process in a CowTerm
// shell has CowTerm as its responsible app, so macOS asks — and remembers —
// Screen Recording on CowTerm's behalf. That memory is keyed on CowTerm's code
// signature, which is why `status` reports it. The OS half of every command is
// CliPlatform.h (Cli-macOS.mm, Cli-Windows.cpp, Cli-Linux.cpp).

#include "Cli.h"
#include "CliPlatform.h"
#include "CowTermVersion.h"
#include "Permissions.h"

#include <eacp/Core/Platform/ScopedAutoReleasePool.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace term::cli
{
namespace
{
// Exit codes agents can branch on.
constexpr int exitOk = 0;
constexpr int exitFail = 1;
constexpr int exitNoPermission = 2;
constexpr int exitNotInCowTerm = 3;

constexpr auto usage = R"(usage: ct <command>

  screen-cap status               granted / not granted, and CowTerm's signature
  screen-cap request              ask macOS for Screen Recording
  screen-cap check                exit 0 if granted, 1 if not; prints nothing
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
  --reset-permissions [service]   tccutil reset <service> (default All) for
                                  CowTerm's bundle id; runs from any shell

Captures default to $COWTERM_CAPTURE_DIR, else ~/Pictures/CowTerm.
Exit codes: 0 ok, 1 error, 2 Screen Recording not granted,
3 not running inside CowTerm (the grant belongs to the hosting terminal).
Windows grants nothing to ask for, and records nothing yet.
)";

int fail(const std::string& message)
{
    std::fprintf(stderr, "ct: %s\n", message.c_str());
    return exitFail;
}

fs::path captureDir()
{
    const auto* dir = std::getenv("COWTERM_CAPTURE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        return dir;

    auto pictures = eacp::toStdPath(eacp::FilePath::picturesDirectory());

    if (pictures.empty())
        pictures = eacp::toStdPath(eacp::FilePath::homeDirectory()) / "Pictures";

    return pictures / "CowTerm";
}

// Resolves -o (or the default name) to an absolute path whose directory exists.
std::optional<fs::path> outputPath(const std::string& requested,
                                   const std::string& prefix,
                                   const std::string& extension)
{
    auto path =
        requested.empty()
            ? captureDir() / (prefix + "-" + platform::timestamp() + extension)
            : fs::path {requested};
    path = fs::absolute(path);

    auto error = std::error_code {};
    fs::create_directories(path.parent_path(), error);

    if (error)
    {
        fail("cannot create " + path.parent_path().string() + ": "
             + error.message());
        return std::nullopt;
    }

    return path;
}

// Without the grant, macOS's screencapture fails ("could not create image from
// display") or, on older systems, quietly records only the wallpaper. Either
// way the agent learns nothing useful, so refuse up front.
int notGranted()
{
    std::fprintf(stderr,
                 "ct: Screen Recording not granted; run: ct screen-cap request\n");
    return exitNoPermission;
}

int status()
{
    const auto ok = platform::captureGranted();
    std::printf("%s\n", ok ? "granted" : "not granted");
    platform::printGrantDetails();
    return ok ? exitOk : exitFail;
}

int request()
{
    return platform::requestCapture() ? exitOk : exitFail;
}

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

    if (!platform::captureGranted())
        return notGranted();

    const auto path = outputPath(options->output, "shot", ".png");
    if (!path)
        return exitFail;

    auto capture = platform::screenshot(*options, *path);

    // An interactive capture cancelled with Escape exits 0 and writes nothing.
    if (capture.status != 0 || !fs::exists(*path))
    {
        auto& message = capture.message;
        while (!message.empty() && message.back() == '\n')
            message.pop_back();
        return fail(message.empty() ? "screen capture produced no image"
                                    : "screen capture: " + message);
    }

    std::printf("%s\n", path->string().c_str());
    return exitOk;
}

// --- recording ---------------------------------------------------------------

struct Recording
{
    long long pid = 0;
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
    return eacp::toStdPath(eacp::FilePath::appDataDirectory()) / "CowTerm" / name;
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
        return at == std::string::npos
                   ? 0
                   : std::atoll(text.c_str() + at + key.size() + 3);
    };

    auto recording = Recording {};
    recording.pid = number("pid");
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
    file << "{\"pid\":" << recording.pid << ",\"path\":\""
         << jsonEscape(recording.path) << "\",\"started\":" << recording.started
         << "}\n";
    return (bool) file;
}

void clearState()
{
    auto error = std::error_code {};
    fs::remove(stateFile(), error);
}

std::optional<Recording> activeRecording()
{
    auto recording = readState();

    if (recording && !platform::isRecorder(recording->pid))
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
        std::fprintf(stderr,
                     "ct: already recording to %s (ct screen-cap record stop)\n",
                     active->path.c_str());
        return exitFail;
    }

    if (!platform::captureGranted())
        return notGranted();

    const auto path = outputPath(options->output, "recording", ".mov");
    if (!path)
        return exitFail;

    auto error = std::string {};
    const auto pid = platform::startRecording(*options, *path, error);

    if (!pid)
        return fail(error);

    if (!writeState({*pid, path->string(), now()}))
        return fail("cannot write " + stateFile().string());

    std::printf("%s\n", path->string().c_str());
    return exitOk;
}

int recordStop()
{
    const auto active = activeRecording();
    if (!active)
        return fail("nothing is recording");

    platform::stopRecording(active->pid);

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds {10};
    while (platform::isRecorder(active->pid)
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds {100});

    while (!fs::exists(active->path) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds {100});

    if (platform::isRecorder(active->pid))
        return fail("recorder (pid " + std::to_string(active->pid)
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
    std::printf("recording\npath: %s\nelapsed: %lld:%02lld\n",
                active->path.c_str(),
                elapsed / 60,
                elapsed % 60);
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
    if (args[0] == "check")
        return platform::captureGranted() ? exitOk : exitFail;
    if (args[0] == "shot")
        return shot(rest);
    if (args[0] == "record")
        return record(rest);

    return fail("unknown screen-cap command: " + args[0]);
}

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

    // Wipes CowTerm's own grants, which is the same from any terminal.
    if (args[0] == "--reset-permissions")
    {
        const auto service = args.size() > 1 ? args[1] : std::string {"All"};
        auto error = std::string {};

        if (!permissions::platform::reset(service, error))
            return fail("reset failed: " + error);

        std::printf("reset %s for %s\n",
                    service.c_str(),
                    permissions::platform::bundleIdentifier().c_str());
        return exitOk;
    }

    // macOS attributes the Screen Recording request to the terminal hosting
    // the shell. Run from iTerm or over ssh, ct would prompt for (and get or be
    // refused by) the wrong app, so the capture commands only run under
    // CowTerm.
    auto parent = std::string {};
    if (!platform::insideCowTerm(parent))
    {
        std::fprintf(
            stderr,
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
// `ct` is the stem, so the name matches ct.exe (and ct.cmd's target) too.
bool isCliInvocation(int argc, char** argv)
{
    if (argc > 0 && fs::path {argv[0]}.stem() == "ct")
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
                             "--version",
                             "--reset-permissions"})
        if (verb == known)
            return true;

    return false;
}

int runCli(int argc, char** argv)
{
    auto pool = eacp::ScopedAutoReleasePool {};
    cli::platform::attachToTerminal();
    return cli::dispatch(std::vector<std::string>(argv + 1, argv + argc));
}
} // namespace term
