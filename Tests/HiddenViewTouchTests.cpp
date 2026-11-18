#include "TerminalViewTestPane.h"

#include "FingerOnWindow.h"
#include "PaneScreen.h"
#include "ScreenStack.h"

// A screen or menu the phone has closed must not take the next touch. These
// route touches from the window's root as eacp does on the phone, and hold
// whether eacp itself skips hidden views or not: a closed screen or menu is gone.

using namespace cowterm::mobile;

namespace
{
constexpr auto phoneWidth = 375.0f;
constexpr auto phoneHeight = 812.0f;
constexpr auto dragDistance = 240.0f;
constexpr auto scrollbackLineCount = 200;
constexpr auto oneMoreLine = "\r\none more line";

// Stands in for the pane screen under the stack: it counts the fingers it gets.
class TouchCountingScreen final : public Graphics::View
{
public:
    TouchCountingScreen() { setHandlesTouchEvents(true); }

    void touchBegan(const Graphics::TouchEvent&) override { ++touchesBegun; }

    int touchesBegun = 0;
};

Graphics::Point getPhoneCentre()
{
    return {phoneWidth / 2.0f, phoneHeight / 2.0f};
}

void layOutAsAPhone(Graphics::View& view)
{
    view.setBounds({0.0f, 0.0f, phoneWidth, phoneHeight});
    // Off-screen, setBounds alone never calls resized().
    view.resized();
}

std::string makeNumberedLines()
{
    auto text = std::string {};

    for (auto line = 1; line <= scrollbackLineCount; ++line)
        text += "line " + std::to_string(line) + (line < scrollbackLineCount ? "\r\n" : "");

    return text;
}

struct PaneScreenOverRecordingShell
{
    PaneScreenOverRecordingShell()
    {
        layOutAsAPhone(screen);
        screen.getTerminal().resized();
        showOutput(makeNumberedLines());
    }

    void showOutput(const std::string& bytes)
    {
        recording->deliverOutput(bytes);
        screen.getTerminal().waitForParsedOutput();

        auto flushed = false;
        Threads::callAsync([&flushed] { flushed = true; });
        Threads::runEventLoopUntil([&flushed] { return flushed; }, outputTimeout);
    }

    static TerminalConfig makeConfig(const std::shared_ptr<ShellRecording>& recording)
    {
        auto config = TerminalConfig {};
        config.makeShell = [recording](const std::string&) { return std::make_unique<RecordingShell>(recording); };
        return config;
    }

    // Only a pane scrolled back counts the lines that arrive.
    bool isScrolledBack()
    {
        showOutput(oneMoreLine);
        return screen.getTerminal().countLinesArrivedWhileScrolledBack() > 0;
    }

    void dragDownThePane()
    {
        const auto start = screen.getTerminal().getBounds().center();
        moveFingerOnWindow(screen, start, {start.x, start.y + dragDistance});
    }

    std::shared_ptr<ShellRecording> recording = std::make_shared<ShellRecording>();
    PaneScreen screen {makeConfig(recording), {}};
};
} // namespace

auto tTapAfterOverview = test("HiddenViews/aTapReachesThePaneScreenOnceTheOverviewCloses") = []
{
    auto paneScreen = TouchCountingScreen {};
    auto screens = ScreenStack {paneScreen};
    layOutAsAPhone(screens);

    screens.showOverview();
    tapWithFingerOnWindow(screens, getPhoneCentre());
    check(paneScreen.touchesBegun == 0, "the open overview takes the tap");

    screens.showPaneScreen();
    check(!screens.findOverview().has_value(), "the closed overview is gone");
    tapWithFingerOnWindow(screens, getPhoneCentre());
    check(paneScreen.touchesBegun == 1, "the tap reaches the pane screen");
};

auto tTapAfterDiffView = test("HiddenViews/aTapReachesThePaneScreenOnceDiffViewCloses") = []
{
    auto paneScreen = TouchCountingScreen {};
    auto screens = ScreenStack {paneScreen};
    layOutAsAPhone(screens);

    screens.showDiffView().showWhyItCannotOpen("dv is not installed on macprorack");
    tapWithFingerOnWindow(screens, getPhoneCentre());
    check(paneScreen.touchesBegun == 0, "dv's screen takes the tap");

    screens.showPaneScreen();
    check(!screens.findDiffView().has_value(), "the closed dv screen is gone");
    tapWithFingerOnWindow(screens, getPhoneCentre());
    check(paneScreen.touchesBegun == 1, "the tap reaches the pane screen");
};

auto tDragAfterMenu = test("HiddenViews/aDragScrollsThePaneOnceTheMenuCloses") = []
{
    auto pane = PaneScreenOverRecordingShell {};
    check(!pane.isScrolledBack(), "the pane starts at its live bottom");

    pane.screen.openMenu();
    pane.screen.closeMenu();
    pane.dragDownThePane();
    check(pane.isScrolledBack(), "the drag scrolls the pane back");
};
