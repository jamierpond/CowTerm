#include "SessionView.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/GPU/GPU.h>
#include <eacp/Graphics/Window/Window.h>

#include <mach/mach.h>

#include <NanoTest/NanoTest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// CowTerm keeps every session's panes alive and shows one session at a time.
// Each pane draws into a Metal layer the size of the pane, and a layer keeps
// its drawables until it is hidden. Before AppShell hid the session it switched
// away from, a CowTerm up for a day held three surfaces per pane for every
// session ever shown: 4.7 GB of IOSurface with 71 sessions.

using namespace nano;
using namespace eacp;
using namespace term;

namespace
{
constexpr auto sessionCount = 6;
constexpr auto framesPerPane = 3;

// A full-screen window on a 14-inch MacBook Pro, in points.
constexpr auto sessionWidth = 1512.f;
constexpr auto sessionHeight = 982.f;

constexpr auto releaseTimeout = Time::MS {5000};
constexpr auto pollInterval = Time::MS {50};
constexpr auto steadyInterval = Time::MS {500};
constexpr auto bytesPerPixel = std::int64_t {4};
constexpr auto bytesPerMegabyte = std::int64_t {1024 * 1024};

std::int64_t getPhysicalFootprint()
{
    auto info = task_vm_info_data_t {};
    auto count = mach_msg_type_number_t {TASK_VM_INFO_COUNT};
    const auto result =
        task_info(mach_task_self(), TASK_VM_INFO, (task_info_t) &info, &count);

    if (result != KERN_SUCCESS)
        return 0;

    return (std::int64_t) info.phys_footprint;
}

std::int64_t waitForFootprintAtMost(std::int64_t limit)
{
    auto deadline = Time::Deadline {releaseTimeout};

    while (getPhysicalFootprint() > limit && !deadline.expired())
        Threads::runEventLoopFor(pollInterval);

    return getPhysicalFootprint();
}

// Rendering leaves transient memory that drains for a second or two; measuring
// from before it settles would credit that to whatever happens next.
std::int64_t waitForSteadyFootprint()
{
    auto deadline = Time::Deadline {releaseTimeout};
    auto previous = getPhysicalFootprint();

    while (!deadline.expired())
    {
        Threads::runEventLoopFor(steadyInterval);
        const auto current = getPhysicalFootprint();

        if (current >= previous - bytesPerMegabyte)
            return current;

        previous = current;
    }

    return previous;
}

std::int64_t toMegabytes(std::int64_t bytes)
{
    return bytes / bytesPerMegabyte;
}

std::vector<SavedPane> twoPaneSplit()
{
    auto root = SavedPane {};
    root.split = true;
    root.horizontal = true;
    root.first = 1;
    root.second = 2;

    return {root, SavedPane {.cwd = "/tmp"}, SavedPane {.cwd = "/tmp"}};
}

Graphics::WindowOptions makeTestWindowOptions()
{
    auto options = Graphics::WindowOptions {};
    options.title = "CowTerm session memory test";
    options.width = (int) sessionWidth;
    options.height = (int) sessionHeight;
    return options;
}

// Renders frames into every pane right away rather than waiting for the window
// to ask. False when a pane got no drawable to render into.
bool renderEveryPane(SessionView& session)
{
    auto renderedAll = true;

    for (auto* pane: session.panes())
        for (auto frame = 0; frame < framesPerPane; ++frame)
            renderedAll &= pane->renderFrame([pane](GPU::Frame& target) { pane->render(target); });

    return renderedAll;
}

// AppShell::attachActive before it hid anything: the old session leaves the
// window, the new one takes its place.
bool attachInPlaceOf(Graphics::View& host, SessionView& shown, SessionView* previous)
{
    if (previous != nullptr)
        host.removeSubview(*previous);

    host.addSubview(shown);
    shown.setBounds({0.f, 0.f, sessionWidth, sessionHeight});
    shown.resized();

    for (auto* pane: shown.panes())
    {
        pane->resizeStarted();
        pane->resized();
    }

    return renderEveryPane(shown);
}

// The least a rendered pane holds: one drawable at its size in pixels.
std::int64_t calculateOneDrawablePerPaneBytes(const SessionView& session)
{
    auto bytes = std::int64_t {0};

    for (const auto* pane: session.panes())
    {
        const auto bounds = pane->getLocalBounds();
        const auto scale = pane->backingScale();
        const auto pixelWidth = (std::int64_t) (bounds.w * scale);
        const auto pixelHeight = (std::int64_t) (bounds.h * scale);
        bytes += pixelWidth * pixelHeight * bytesPerPixel;
    }

    return bytes;
}
} // namespace

auto tHiddenSessionsFreeTheirFrames =
    test("SessionMemory/hiddenSessionsFreeTheirPanesFrames") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto config = AppConfig {};
    auto host = Graphics::View {};
    auto window = Graphics::Window {makeTestWindowOptions()};
    window.setContentView(host);

    auto sessions = std::vector<std::unique_ptr<SessionView>> {};
    auto* previous = (SessionView*) nullptr;

    for (auto index = 0; index < sessionCount; ++index)
    {
        auto& session = *sessions.emplace_back(std::make_unique<SessionView>(config, "/tmp"));
        session.restore(twoPaneSplit());
        check(attachInPlaceOf(host, session, previous), "every pane renders a frame");
        previous = &session;
    }

    const auto everySessionRendered = waitForSteadyFootprint();
    auto leastFreedByHiding = std::int64_t {0};

    // What AppShell::attachActive does now to every session it switches away
    // from.
    for (auto index = 0; index < sessionCount - 1; ++index)
    {
        leastFreedByHiding += calculateOneDrawablePerPaneBytes(*sessions[(size_t) index]);
        sessions[(size_t) index]->setVisible(false);
    }

    const auto freed =
        everySessionRendered - waitForFootprintAtMost(everySessionRendered - leastFreedByHiding);

    LOG("SessionMemory: hiding " + std::to_string(sessionCount - 1) + " sessions freed "
        + std::to_string(toMegabytes(freed)) + " MB, at least "
        + std::to_string(toMegabytes(leastFreedByHiding)) + " MB expected");

    check(freed >= leastFreedByHiding, "a hidden session's panes keep no drawables");
};
