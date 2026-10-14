#include "Permissions.h"

#include "CowTermCore/Debug.h"

#include <eacp/Core/App/App.h>
#include <eacp/Graphics/Graphics.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <dirent.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <climits>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

extern char** environ;

@interface CowTermAppDragSource : NSObject <NSDraggingSource>
@end

@implementation CowTermAppDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    return NSDragOperationCopy | NSDragOperationLink | NSDragOperationGeneric;
}
@end

namespace term::permissions::platform
{
namespace fs = std::filesystem;

namespace
{
constexpr auto finder = "com.apple.finder";
constexpr auto systemEvents = "com.apple.systemevents";

// Notification settings only come back asynchronously, so check() reports the
// last answer and asks again for the next one.
std::atomic<Status> notificationStatus {Status::notGranted};

// Set once Grant has asked for the three folders: probing them before that
// would be the prompt itself.
std::atomic<bool> foldersAsked {false};

void onMain(std::function<void()> done)
{
    auto shared = std::make_shared<std::function<void()>>(std::move(done));
    dispatch_async(dispatch_get_main_queue(), ^{
      (*shared)();
    });
}

void inBackground(std::function<void()> work)
{
    auto shared = std::make_shared<std::function<void()>>(std::move(work));
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      (*shared)();
    });
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

std::string home()
{
    const auto* value = std::getenv("HOME");
    return value != nullptr ? value : "";
}

NSString* toNSString(const std::string& text)
{
    return [NSString stringWithUTF8String:text.c_str()];
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
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    auto argv = std::vector<char*> {};
    for (const auto& arg: args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    auto pid = pid_t {};
    const auto spawned = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (spawned == 0)
    {
        char buffer[512];
        for (auto n = read(fds[0], buffer, sizeof buffer); n > 0;
             n = read(fds[0], buffer, sizeof buffer))
            result.text.append(buffer, (std::size_t) n);

        auto status = 0;
        if (waitpid(pid, &status, 0) == pid && WIFEXITED(status))
            result.status = WEXITSTATUS(status);
    }

    close(fds[0]);
    return result;
}

// The verdict for one target app. A target that isn't running has none.
enum class Automation
{
    granted,
    denied,
    notDetermined,
    unavailable
};

Automation automation(const char* bundleId, bool prompt)
{
    auto target = AEDesc {};
    if (AECreateDesc(typeApplicationBundleID,
                     bundleId,
                     (Size) std::strlen(bundleId),
                     &target)
        != noErr)
        return Automation::unavailable;

    const auto verdict =
        AEDeterminePermissionToAutomateTarget(&target, typeWildCard, typeWildCard, prompt);
    AEDisposeDesc(&target);

    switch (verdict)
    {
        case noErr:
            return Automation::granted;
        case errAEEventNotPermitted:
            return Automation::denied;
        case errAEEventWouldRequireUserConsent:
            return Automation::notDetermined;
        default:
            return Automation::unavailable;
    }
}

Status automationStatus()
{
    auto any = false;

    for (const auto* target: {finder, systemEvents})
    {
        switch (automation(target, false))
        {
            case Automation::denied:
                return Status::denied;
            case Automation::notDetermined:
                return Status::notGranted;
            case Automation::granted:
                any = true;
                break;
            case Automation::unavailable:
                break;
        }
    }

    return any ? Status::granted : Status::notGranted;
}

// No API reports Full Disk Access; reading the user's TCC database is the
// standard test, since nothing short of it opens that file.
Status fullDiskAccessStatus()
{
    const auto path = home() + "/Library/Application Support/com.apple.TCC/TCC.db";
    const auto fd = open(path.c_str(), O_RDONLY);

    if (fd >= 0)
    {
        close(fd);
        return Status::granted;
    }

    return Status::notGranted;
}

const std::vector<std::string>& protectedFolders()
{
    static const auto folders = std::vector<std::string> {
        home() + "/Desktop", home() + "/Documents", home() + "/Downloads"};
    return folders;
}

bool canList(const std::string& folder)
{
    auto* dir = opendir(folder.c_str());

    if (dir == nullptr)
        return false;

    closedir(dir);
    return true;
}

Status foldersStatus()
{
    if (fullDiskAccessStatus() == Status::granted)
        return Status::granted;

    if (!foldersAsked)
        return Status::unknown;

    for (const auto& folder: protectedFolders())
        if (!canList(folder))
            return Status::notGranted;

    return Status::granted;
}

void refreshNotifications(std::function<void()> done = {})
{
    if (NSBundle.mainBundle.bundleIdentifier == nil)
        return;

    auto shared = std::make_shared<std::function<void()>>(std::move(done));

    [UNUserNotificationCenter.currentNotificationCenter
        getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
          switch (settings.authorizationStatus)
          {
              case UNAuthorizationStatusAuthorized:
              case UNAuthorizationStatusProvisional:
                  notificationStatus = Status::granted;
                  break;
              case UNAuthorizationStatusDenied:
                  notificationStatus = Status::denied;
                  break;
              default:
                  notificationStatus = Status::notGranted;
                  break;
          }

          if (*shared)
              onMain(*shared);
        }];
}

// Automation can only be asked of a running app; System Events runs on demand.
void launchSystemEvents(std::function<void()> then)
{
    auto shared = std::make_shared<std::function<void()>>(std::move(then));

    if ([NSRunningApplication runningApplicationsWithBundleIdentifier:toNSString(systemEvents)]
            .count
        > 0)
    {
        inBackground(*shared);
        return;
    }

    auto* url = [NSWorkspace.sharedWorkspace
        URLForApplicationWithBundleIdentifier:toNSString(systemEvents)];

    if (url == nil)
    {
        inBackground(*shared);
        return;
    }

    auto* configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = NO;
    configuration.hides = YES;

    [NSWorkspace.sharedWorkspace openApplicationAtURL:url
                                        configuration:configuration
                                    completionHandler:^(NSRunningApplication*, NSError*) {
                                      inBackground(*shared);
                                    }];
}
} // namespace

Status check(Kind kind)
{
    switch (kind)
    {
        case Kind::screenRecording:
            return CGPreflightScreenCaptureAccess() ? Status::granted : Status::notGranted;
        case Kind::accessibility:
            return AXIsProcessTrusted() ? Status::granted : Status::notGranted;
        case Kind::fullDiskAccess:
            return fullDiskAccessStatus();
        case Kind::automation:
            return automationStatus();
        case Kind::filesAndFolders:
            return foldersStatus();
        case Kind::notifications:
            refreshNotifications();
            return notificationStatus;
    }

    return Status::unknown;
}

void probeScreenRecording(std::function<void(bool)> done)
{
    // `ct screen-cap check` is this same binary as a fresh process, charged to
    // this app (its parent), so it reads TCC as it stands now.
    const auto self = executablePath().string();

    inBackground(
        [self, done = std::move(done)]
        {
            const auto granted = run({self, "screen-cap", "check"}).status == 0;
            onMain([done, granted] { done(granted); });
        });
}

void request(Kind kind, bool prompted, std::function<void()> done)
{
    switch (kind)
    {
        case Kind::screenRecording:
            // Adds CowTerm to the list (switched off) and shows macOS's
            // prompt, once; from then on it only answers.
            if (prompted || CGRequestScreenCaptureAccess())
                openSettings(kind);
            break;

        case Kind::accessibility:
            if (prompted)
                openSettings(kind);
            else
            {
                NSDictionary* options = @{(__bridge NSString*) kAXTrustedCheckOptionPrompt: @YES};
                AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef) options);
            }
            break;

        case Kind::fullDiskAccess:
            openSettings(kind);
            break;

        case Kind::automation:
            if (prompted || automationStatus() == Status::denied)
            {
                openSettings(kind);
                break;
            }

            // AEDeterminePermissionToAutomateTarget blocks until the prompt
            // is answered, so never on the main thread.
            launchSystemEvents(
                [done]
                {
                    for (const auto* target: {finder, systemEvents})
                        automation(target, true);

                    onMain(done);
                });
            return;

        case Kind::filesAndFolders:
            if (prompted)
            {
                openSettings(kind);
                break;
            }

            // Touching each folder is the request: macOS prompts on first
            // access, and the access waits for the answer.
            inBackground(
                [done]
                {
                    for (const auto& folder: protectedFolders())
                        canList(folder);

                    foldersAsked = true;
                    onMain(done);
                });
            return;

        case Kind::notifications:
            if (notificationStatus != Status::notGranted || prompted)
            {
                openSettings(kind);
                break;
            }

            {
                auto shared = std::make_shared<std::function<void()>>(done);
                [UNUserNotificationCenter.currentNotificationCenter
                    requestAuthorizationWithOptions:(UNAuthorizationOptionAlert
                                                     | UNAuthorizationOptionSound)
                                  completionHandler:^(BOOL, NSError*) {
                                    refreshNotifications(*shared);
                                  }];
            }
            return;
    }

    done();
}

bool openSettings(Kind kind)
{
    auto* url = [NSURL URLWithString:toNSString(settingsUrl(kind, bundleIdentifier()))];
    return url != nil && [NSWorkspace.sharedWorkspace openURL:url];
}

bool startAppDrag(eacp::Graphics::View& view)
{
    auto* nsView = (__bridge NSView*) view.getHandle();
    auto* event = NSApp.currentEvent;
    const auto path = bundlePath();

    if (nsView == nil || event == nil || path.empty())
        return false;

    static auto* source = [CowTermAppDragSource new];

    auto* filePath = toNSString(path);
    auto* item = [[[NSDraggingItem alloc]
        initWithPasteboardWriter:[NSURL fileURLWithPath:filePath]] autorelease];

    auto* image = [NSWorkspace.sharedWorkspace iconForFile:filePath];
    image.size = NSMakeSize(64, 64);

    auto at = [nsView convertPoint:event.locationInWindow fromView:nil];
    [item setDraggingFrame:NSMakeRect(at.x - 32, at.y - 32, 64, 64) contents:image];

    return [nsView beginDraggingSessionWithItems:@[item] event:event source:source] != nil;
}

eacp::Graphics::Image appIcon()
{
    const auto path = bundlePath();

    if (path.empty())
        return {};

    auto* icon = [NSWorkspace.sharedWorkspace iconForFile:toNSString(path)];
    auto rect = NSMakeRect(0, 0, 128, 128);
    auto image = [icon CGImageForProposedRect:&rect context:nil hints:nil];

    if (image == nullptr)
        return {};

    auto* bitmap = [[[NSBitmapImageRep alloc] initWithCGImage:image] autorelease];
    auto* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];

    if (png == nil)
        return {};

    return eacp::Graphics::Image::decode((const std::uint8_t*) png.bytes, (int) png.length);
}

std::string bundlePath()
{
    // Through ~/.local/bin/ct's symlink too, since executablePath() resolves it.
    for (auto path = executablePath(); path.has_parent_path() && path != path.root_path();
         path = path.parent_path())
        if (path.extension() == ".app")
            return path.string();

    return {};
}

std::string bundleIdentifier()
{
    const auto path = bundlePath();
    auto* bundle = path.empty() ? NSBundle.mainBundle : [NSBundle bundleWithPath:toNSString(path)];
    auto* identifier = bundle.bundleIdentifier;
    return identifier != nil ? std::string {identifier.UTF8String} : std::string {};
}

bool reset(const std::string& service, std::string& error)
{
    const auto id = bundleIdentifier();

    if (id.empty())
    {
        error = "not running from an app bundle, so there is no bundle id to reset";
        return false;
    }

    const auto result = run(resetCommand(id, service));

    if (result.status == 0)
    {
        foldersAsked = false;
        return true;
    }

    error = result.text.empty() ? "tccutil failed" : result.text;
    return false;
}

void relaunch()
{
    const auto path = bundlePath();

    if (path.empty())
        return;

    const auto args = relaunchCommand((int) getpid(), path);

    auto argv = std::vector<char*> {};
    for (const auto& arg: args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    // Its own session, so it outlives this process; launchd adopts it.
    auto attributes = posix_spawnattr_t {};
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT);

    auto actions = posix_spawn_file_actions_t {};
    posix_spawn_file_actions_init(&actions);
    for (auto fd: {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})
        posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", O_RDWR, 0);

    auto pid = pid_t {};
    const auto spawned =
        posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);

    if (spawned != 0)
        return;

    // The ordinary quit: the daemon keeps every shell, and the new process
    // re-adopts them on launch.
    noteQuit("permissions relaunch");
    eacp::Apps::quit();
}
} // namespace term::permissions::platform
