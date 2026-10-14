#pragma once

#include <functional>
#include <string>
#include <vector>

namespace eacp::Graphics
{
class Image;
class View;
} // namespace eacp::Graphics

// The macOS privacy grants (TCC) CowTerm asks for. A terminal needs them less
// for itself than for what it runs: macOS charges a command to the app that
// hosts its shell, so `screencapture`, `osascript` or a shell reading
// ~/Library/Mail get CowTerm's grants or none.
//
// The model here is pure, so it is tested anywhere; the platform half (live
// checks, prompts, System Settings) is Permissions-macOS.mm. Windows has no
// such grants and builds neither.
namespace term::permissions
{
enum class Kind
{
    screenRecording,
    accessibility,
    fullDiskAccess,
    automation,
    filesAndFolders,
    notifications
};

enum class Status
{
    granted,

    // Granted in TCC, but this process still sees the old answer: macOS
    // caches Screen Recording per process, so it applies from the next launch.
    needsRelaunch,

    // Off, or never asked.
    notGranted,

    // Asked and refused: macOS will not prompt again, only Settings helps.
    denied,

    // Checking would itself prompt (Files & Folders), so it waits for Grant.
    unknown
};

struct Info
{
    Kind kind;
    std::string name;
    std::string why;
    bool required = false;

    // System Settings' name for the list, for the steps.
    std::string listName;

    // The list takes an app dragged onto it (or added with +).
    bool dragToList = false;

    // macOS has an API that shows its own prompt for this one.
    bool canPrompt = false;
};

// Every grant, in the order the panel lists them: required first.
const std::vector<Info>& all();
const Info& info(Kind kind);

// The exact System Settings pane. Notifications are per app, so they need
// the bundle id.
std::string settingsUrl(Kind kind, const std::string& bundleId);

struct Entry
{
    Kind kind;
    Status status;
};

// What the panel shows on launch for: a required grant this process can't use.
bool requiredMissing(const std::vector<Entry>& entries);

// Every required grant in force now, so an automatically shown panel can go.
bool requiredSatisfied(const std::vector<Entry>& entries);

std::string statusLabel(Status status);

// The row's button: "" for a grant already in force. `prompted` is whether
// Grant was pressed already this run; macOS shows its prompt once, after that
// only System Settings can turn the grant on.
std::string actionLabel(const Info& grant, Status status, bool prompted);

// tccutil's argv for wiping one service (or All) for one bundle id.
std::vector<std::string> resetCommand(const std::string& bundleId,
                                      const std::string& service = "All");

// /bin/sh argv that waits for `pid` to exit, then opens `bundlePath` again.
std::vector<std::string> relaunchCommand(int pid, const std::string& bundlePath);

namespace platform
{
// Live, never prompts. Cheap except where noted in the .mm.
Status check(Kind kind);

// Screen Recording as a fresh process sees it, asynchronously (it runs `ct`):
// the only way to see a grant this process has cached the absence of.
// `done` runs on the main thread.
void probeScreenRecording(std::function<void(bool granted)> done);

// The system prompt where there is one, otherwise (or the second time) the
// System Settings pane. Asynchronous where macOS blocks; `done` runs on the
// main thread once the answer may have changed.
void request(Kind kind, bool prompted, std::function<void()> done);

bool openSettings(Kind kind);

// An OS drag of CowTerm.app from `view`, during its mouse-down.
bool startAppDrag(eacp::Graphics::View& view);

eacp::Graphics::Image appIcon();

std::string bundleIdentifier();
std::string bundlePath();

// tccutil reset <service> for this app's bundle id, as the user (no sudo). The
// daemon needs nothing of its own: tccutil can't resolve its id, and every
// shell it runs is charged to the app anyway. False, with tccutil's words in
// `error`, if it failed.
bool reset(const std::string& service, std::string& error);

// Quits (the daemon keeps every shell) and opens CowTerm again.
void relaunch();
} // namespace platform
} // namespace term::permissions
