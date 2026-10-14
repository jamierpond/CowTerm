#pragma once

#include <filesystem>
#include <optional>
#include <string>

// What `ct` needs from the OS: the screen-capture grant, the capture tools and
// the process tree. Everything else -- verbs, options, where captures go, the
// recording's state file -- is Cli.cpp, the same on every platform.
namespace term::cli
{
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

struct Capture
{
    int status = -1;

    // What the capture tool said, for the error when it fails.
    std::string message;
};

namespace platform
{
// Connects ct's output to the terminal that ran it, where the OS leaves a GUI
// executable without one.
void attachToTerminal();

// "20260101-120000" in local time, for default capture names.
std::string timestamp();

// Whether a capture can run now. Never prompts.
bool captureGranted();

// Asks for the grant, printing the outcome and, if refused, how to give it.
bool requestCapture();

// What `screen-cap status` adds after granted / not granted: the app the
// grant belongs to, and anything that would keep it from sticking.
void printGrantDetails();

// A still of the screen (or the display, window or rect in `options`) as a
// PNG at `path`. An interactive capture cancelled with Escape succeeds and
// writes nothing.
Capture screenshot(const Options& options, const std::filesystem::path& path);

// Starts a recording to `path` that outlives ct, returning the recorder's
// pid, or nothing with the reason in `error`.
std::optional<long long> startRecording(const Options& options,
                                        const std::filesystem::path& path,
                                        std::string& error);

// Whether `pid` is still a recorder; the pid in the state file could have been
// recycled since it was written.
bool isRecorder(long long pid);

// Asks the recorder to finish the movie and exit, without waiting for it.
void stopRecording(long long pid);

// Whether ct runs under CowTerm (some ancestor is its app or its daemon),
// where that matters for the grant. `parent` names the nearest ancestor for
// the error when it isn't.
bool insideCowTerm(std::string& parent);
} // namespace platform
} // namespace term::cli
