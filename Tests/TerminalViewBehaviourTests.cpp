#include <TerminalView/TerminalView.h>
#include <Emulator/TerminalModel.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// What a pane sends its shell and draws for a given input, pinned through the
// widget's public surface: key presses, mouse events and copy mode. The shell
// is a recording fake, so every byte the view writes can be checked exactly and
// output reaches the screen without a process behind it.
//
// The pixel checks self-skip without a GPU device, as RenderBackgroundTests do.

using namespace nano;
using namespace eacp;
using namespace term;

namespace
{
constexpr auto viewWidth = 420.f;
constexpr auto viewHeight = 160.f;
constexpr auto marginX = 6.f;
constexpr auto marginY = 4.f;
constexpr auto samplingInsetInPoints = 1.f;
constexpr auto colourTolerance = 0.08f;
constexpr auto outputTimeout = Time::MS {4000};

constexpr std::uint16_t macKeypadEnter = 0x4c;
constexpr std::uint16_t macForwardDelete = 0x75;
constexpr std::uint16_t macHome = 0x73;
constexpr std::uint16_t macEnd = 0x77;
constexpr std::uint16_t macPageUp = 0x74;
constexpr std::uint16_t macPageDown = 0x79;

struct ShellRecording
{
    std::string written;
    std::vector<PtySize> resizes;
    int sizeTakeBacks = 0;
    std::function<void(std::optional<PtySize>)> setGridHeldElsewhere;
    std::function<void(const std::string&)> showStatus;
    std::function<void(std::string)> deliverOutput;
    std::function<void(const std::string&)> deliverRestoredState;
};

class RecordingShell final : public Shell
{
public:
    explicit RecordingShell(std::shared_ptr<ShellRecording> recordingToUse)
        : recording(std::move(recordingToUse))
    {
    }

    bool start(const PtyOptions&,
               std::function<void(std::string)> onOutput,
               std::function<void()>) override
    {
        recording->deliverOutput = std::move(onOutput);
        recording->deliverRestoredState = onRestoreState;
        recording->setGridHeldElsewhere = onGridSetElsewhere;
        recording->showStatus = onStatusChanged;
        return true;
    }

    void write(std::string_view data) override { recording->written += data; }
    void resize(const PtySize& size) override { recording->resizes.push_back(size); }
    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }
    void terminate() override {}
    void detach() override {}
    void takeSizeBack() override { ++recording->sizeTakeBacks; }

private:
    std::shared_ptr<ShellRecording> recording;
};

struct TestPane
{
    TestPane()
    {
        auto config = TerminalConfig {};
        config.makeShell = [this](const std::string&)
        { return std::make_unique<RecordingShell>(recording); };

        view = std::make_unique<TerminalView>(config, "/tmp");
        view->setBounds({0.f, 0.f, viewWidth, viewHeight});
        // Off-screen, setBounds alone never calls resized(), which builds the renderers.
        view->resized();
    }

    void showOutput(const std::string& bytes)
    {
        recording->deliverOutput(bytes);
        view->waitForParsedOutput();

        auto flushed = false;
        Threads::callAsync([&flushed] { flushed = true; });
        Threads::runEventLoopUntil([&flushed] { return flushed; }, outputTimeout);
    }

    std::string takeWritten()
    {
        auto written = std::string {};
        written.swap(recording->written);
        return written;
    }

    std::string writtenForKey(const Graphics::KeyEvent& event)
    {
        takeWritten();
        view->keyDown(event);
        return takeWritten();
    }

    std::shared_ptr<ShellRecording> recording = std::make_shared<ShellRecording>();
    std::unique_ptr<TerminalView> view;
};

Graphics::KeyEvent keyWithCode(std::uint16_t keyCode, Graphics::ModifierKeys mods = {})
{
    auto event = Graphics::KeyEvent {};
    event.keyCode = keyCode;
    event.modifiers = mods;
    return event;
}

Graphics::KeyEvent typedKey(const std::string& text, Graphics::ModifierKeys mods = {})
{
    auto event = Graphics::KeyEvent {};
    event.characters = text;
    event.charactersIgnoringModifiers = text;
    event.keyCode = 0xffff;
    event.modifiers = mods;
    return event;
}

Graphics::ModifierKeys shiftHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.shift = true;
    return mods;
}

Graphics::ModifierKeys controlHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.control = true;
    return mods;
}

Graphics::ModifierKeys altHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.alt = true;
    return mods;
}

Graphics::ModifierKeys commandHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.command = true;
    return mods;
}

GlyphAtlas& referenceAtlas()
{
    static auto atlas = GlyphAtlas {TerminalConfig {}.font, TerminalConfig {}.fontSize};
    return atlas;
}

Graphics::Point centreOfCell(int col, int row)
{
    const auto& atlas = referenceAtlas();
    return {marginX + ((float) col + 0.5f) * atlas.cellWidth(),
            marginY + ((float) row + 0.5f) * atlas.cellHeight()};
}

Graphics::MouseEvent mouseAtCell(int col, int row)
{
    auto event = Graphics::MouseEvent {};
    event.pos = centreOfCell(col, row);
    return event;
}

Graphics::MouseEvent wheelBy(float lines)
{
    auto event = mouseAtCell(0, 0);
    event.delta = {0.f, lines};
    return event;
}

Graphics::MouseEvent trackpadScrollBy(float lines)
{
    auto event = mouseAtCell(0, 0);
    event.delta = {0.f, lines * referenceAtlas().cellHeight()};
    event.preciseScrolling = true;
    return event;
}

bool matches(const Graphics::Color& colour, Rgb expected)
{
    const auto target = toColor(expected);

    return std::abs(colour.r - target.r) < colourTolerance
           && std::abs(colour.g - target.g) < colourTolerance
           && std::abs(colour.b - target.b) < colourTolerance;
}

// The colour just inside a cell's top-left corner, where lowercase letters
// leave the background showing.
bool isCellPainted(const Graphics::Image& image, int col, int row, Rgb expected)
{
    const auto& atlas = referenceAtlas();
    const auto x = marginX + (float) col * atlas.cellWidth() + samplingInsetInPoints;
    const auto y = marginY + (float) row * atlas.cellHeight() + samplingInsetInPoints;
    return matches(image.at((int) std::ceil(x), (int) std::ceil(y)), expected);
}

const Theme defaultTheme {};
} // namespace

// ---- keys -------------------------------------------------------------------

auto tEnterAndKeypadEnter = test("View/enterSendsCarriageReturn") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::Return)) == "\r");
    check(pane.writtenForKey(keyWithCode(macKeypadEnter)) == "\r");
};

auto tTabAndBackTab = test("View/tabAndShiftTab") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::Tab)) == "\t");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::Tab, shiftHeld()))
          == "\033[Z");
};

auto tEscapeAndDelete = test("View/escapeBackspaceAndForwardDelete") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::Escape)) == "\033");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::Delete)) == "\x7f");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::Delete, altHeld()))
          == "\033\x7f");
    check(pane.writtenForKey(keyWithCode(macForwardDelete)) == "\033[3~");
    check(pane.writtenForKey(keyWithCode(macForwardDelete, shiftHeld()))
          == "\033[3;2~");
};

auto tArrowsInNormalMode = test("View/arrowsSendCsiWithModifiers") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::UpArrow)) == "\033[A");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::DownArrow)) == "\033[B");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::RightArrow)) == "\033[C");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::LeftArrow)) == "\033[D");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::UpArrow, controlHeld()))
          == "\033[1;5A");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::LeftArrow, altHeld()))
          == "\033[1;3D");
};

auto tArrowsInApplicationMode = test("View/applicationCursorKeysUseSs3") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1h");

    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::UpArrow)) == "\033OA");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::LeftArrow)) == "\033OD");
    check(pane.writtenForKey(keyWithCode(macHome)) == "\033OH");
    check(pane.writtenForKey(keyWithCode(macEnd)) == "\033OF");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::UpArrow, shiftHeld()))
          == "\033[1;2A");
};

auto tHomeEndPages = test("View/homeEndAndPageKeys") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(keyWithCode(macHome)) == "\033[H");
    check(pane.writtenForKey(keyWithCode(macEnd)) == "\033[F");
    check(pane.writtenForKey(keyWithCode(macPageUp)) == "\033[5~");
    check(pane.writtenForKey(keyWithCode(macPageDown)) == "\033[6~");
    check(pane.writtenForKey(keyWithCode(macPageDown, controlHeld())) == "\033[6;5~");
};

auto tShiftedNavigationScrolls = test("View/shiftedNavigationScrollsInsteadOfSending") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(keyWithCode(macHome, shiftHeld())).empty());
    check(pane.writtenForKey(keyWithCode(macEnd, shiftHeld())).empty());
    check(pane.writtenForKey(keyWithCode(macPageUp, shiftHeld())).empty());
    check(pane.writtenForKey(keyWithCode(macPageDown, shiftHeld())).empty());
};

auto tFunctionKeys = test("View/functionKeys") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F1)) == "\033OP");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F2)) == "\033OQ");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F3)) == "\033OR");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F4)) == "\033OS");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F5)) == "\033[15~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F6)) == "\033[17~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F7)) == "\033[18~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F8)) == "\033[19~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F9)) == "\033[20~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F10)) == "\033[21~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F11)) == "\033[23~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F12)) == "\033[24~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F5, controlHeld()))
          == "\033[15;5~");
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F1, shiftHeld())) == "\033OP");
};

auto tTypedText = test("View/typedTextControlSpaceAndAltPrefix") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(typedKey("a")) == "a");
    check(pane.writtenForKey(typedKey(" ", controlHeld())) == std::string("\0", 1));
    check(pane.writtenForKey(typedKey("x", altHeld())) == "\033x");

    auto controlC = typedKey("c", controlHeld());
    controlC.characters = "\003";
    check(pane.writtenForKey(controlC) == "\003");
};

auto tCommandShortcuts = test("View/commandShortcutsNeverReachTheShell") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(typedKey("k", commandHeld())) == "\014");
    check(pane.writtenForKey(typedKey("z", commandHeld())).empty());
};

auto tInterceptConsumes = test("View/interceptedKeysAreNotSent") = []
{
    auto pane = TestPane {};
    pane.view->interceptKey = [](const Graphics::KeyEvent&) { return true; };
    check(pane.writtenForKey(typedKey("a")).empty());
};

auto tSendTextTurnsNewlines = test("View/sendTextPressesEnterForNewlines") = []
{
    auto pane = TestPane {};
    pane.view->sendText("ls\nexit\n");
    check(pane.takeWritten() == "ls\rexit\r");
};

auto tComposedTextIsBracketed = test("View/composedTextGoesAsABracketedPasteThenEnter") = []
{
    constexpr auto bracketedPasteOn = "\x1b[?2004h";
    auto pane = TestPane {};
    pane.showOutput(bracketedPasteOn);

    pane.view->pasteThenPressEnter("git status");
    check(pane.takeWritten() == "\x1b[200~git status\x1b[201~\r");
};

auto tComposedLinesAsk = test("View/composedLinesAskBeforeReachingAShellWithoutBracketedPaste") = []
{
    auto pane = TestPane {};
    auto askedAbout = std::string {};
    auto sendAfterAsking = Callback {};
    pane.view->confirmUnsafePaste = [&](const std::string& text, const Callback& paste)
    {
        askedAbout = text;
        sendAfterAsking = paste;
    };

    pane.view->pasteThenPressEnter("rm -rf build\nmake");
    check(askedAbout == "rm -rf build\nmake");
    check(pane.takeWritten().empty());

    sendAfterAsking();
    check(pane.takeWritten() == "rm -rf build\rmake\r");
};

// ---- mouse ------------------------------------------------------------------

auto tSgrMouseReports = test("View/sgrMouseReportsClicksAndReleases") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1000h\033[?1006h");
    pane.takeWritten();

    pane.view->mouseDown(mouseAtCell(2, 1));
    check(pane.takeWritten() == "\033[<0;3;2M");

    pane.view->mouseUp(mouseAtCell(2, 1));
    check(pane.takeWritten() == "\033[<0;3;2m");

    pane.view->mouseDragged(mouseAtCell(4, 1));
    check(pane.takeWritten().empty(), "button-only tracking reports no motion");
};

auto tSgrMouseDrag = test("View/sgrMouseReportsDragsAsMotion") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1002h\033[?1006h");
    pane.takeWritten();

    pane.view->mouseDragged(mouseAtCell(4, 0));
    check(pane.takeWritten() == "\033[<32;5;1M");
};

auto tSgrWheel = test("View/sgrMouseReportsTheWheel") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1000h\033[?1006h");
    pane.takeWritten();

    pane.view->mouseWheel(wheelBy(1.f));
    check(pane.takeWritten() == "\033[<64;1;1M");

    pane.view->mouseWheel(wheelBy(-1.f));
    check(pane.takeWritten() == "\033[<65;1;1M");
};

auto tTrackpadWheelReports = test("View/trackpadSendsOneWheelReportPerLine") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1000h\033[?1006h");
    pane.takeWritten();

    constexpr auto fractionOfALine = 0.25f;
    constexpr auto tinyEventsInOneLine = 4;

    for (auto i = 0; i < tinyEventsInOneLine - 1; ++i)
        pane.view->mouseWheel(trackpadScrollBy(fractionOfALine));

    check(pane.takeWritten().empty(), "a part line of finger travel sends nothing");

    pane.view->mouseWheel(trackpadScrollBy(fractionOfALine));
    check(pane.takeWritten() == "\033[<64;1;1M", "the event completing the line sends one");

    pane.view->mouseWheel(trackpadScrollBy(-2.f));
    check(pane.takeWritten() == "\033[<65;1;1M\033[<65;1;1M");
};

auto tShiftBypassesReporting = test("View/shiftClickSelectsEvenWithMouseReporting") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1000h\033[?1006h");
    pane.takeWritten();

    auto event = mouseAtCell(2, 1);
    event.modifiers = shiftHeld();
    pane.view->mouseDown(event);
    check(pane.takeWritten().empty());
};

auto tNonSgrReportsNothing = test("View/mouseModeWithoutSgrSendsNothing") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1000h");
    pane.takeWritten();

    pane.view->mouseDown(mouseAtCell(2, 1));
    check(pane.takeWritten().empty());
};

auto tWheelInAltScreen = test("View/wheelInAltScreenSendsArrowKeys") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1049h");
    pane.takeWritten();

    pane.view->mouseWheel(wheelBy(3.f));
    check(pane.takeWritten() == "\033[A\033[A\033[A");

    pane.view->mouseWheel(wheelBy(-2.f));
    check(pane.takeWritten() == "\033[B\033[B");

    pane.view->mouseWheel(wheelBy(0.5f));
    check(pane.takeWritten().empty(), "half a line waits for the rest");

    pane.view->mouseWheel(wheelBy(0.5f));
    check(pane.takeWritten() == "\033[A");

    pane.view->mouseWheel(wheelBy(100.f));
    auto expected = std::string {};

    for (auto i = 0; i < 40; ++i)
        expected += "\033[A";

    check(pane.takeWritten() == expected, "a big flick is capped at forty arrows");
};

auto tWheelInAltScreenAppKeys = test("View/wheelInAltScreenHonoursApplicationKeys") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1049h\033[?1h");
    pane.takeWritten();

    pane.view->mouseWheel(wheelBy(1.f));
    check(pane.takeWritten() == "\033OA");

    pane.view->mouseWheel(wheelBy(-1.f));
    check(pane.takeWritten() == "\033OB");
};

// ---- scrolling the scrollback -----------------------------------------------

namespace
{
std::string makeLinesOfOutput(int count)
{
    auto output = std::string {};

    for (auto line = 0; line < count; ++line)
        output += "line " + std::to_string(line) + "\r\n";

    return output;
}

Graphics::TouchEvent touchAt(Graphics::Point position, double timestampSeconds)
{
    auto touch = Graphics::TouchEvent {};
    touch.pos = position;
    touch.timestamp = timestampSeconds;
    return touch;
}

Graphics::TouchEvent touchMovedDownByRows(const Graphics::TouchEvent& start,
                                          float rows,
                                          double timestampSeconds)
{
    const auto travel = rows * referenceAtlas().cellHeight();
    const auto moved = Graphics::Point {start.pos.x, start.pos.y + travel};
    return touchAt(moved, timestampSeconds);
}
} // namespace

auto tScrolledBackCountsNewLines = test("View/scrolledBackTheViewCountsLinesThatArrive") = []
{
    constexpr auto linesOfHistory = 50;
    constexpr auto linesArrivingLater = 3;
    constexpr auto wheelLinesBack = 5.f;
    auto pane = TestPane {};
    pane.showOutput(makeLinesOfOutput(linesOfHistory));

    pane.view->mouseWheel(wheelBy(wheelLinesBack));
    pane.showOutput(makeLinesOfOutput(linesArrivingLater));
    check(pane.view->countLinesArrivedWhileScrolledBack() == linesArrivingLater);

    pane.view->keyDown(typedKey("x"));
    check(pane.view->countLinesArrivedWhileScrolledBack() == 0,
          "typing returns to the live bottom");
};

auto tTouchDragScrollsBack = test("View/aFingerDraggedDownScrollsTheScrollbackBack") = []
{
    constexpr auto linesOfHistory = 50;
    constexpr auto rowsDragged = 2.5f;
    constexpr auto moveTime = 0.02;
    constexpr auto liftTime = 0.03;
    auto pane = TestPane {};
    pane.showOutput(makeLinesOfOutput(linesOfHistory));
    pane.takeWritten();

    const auto start = touchAt(centreOfCell(0, 0), 0.0);
    const auto moved = touchMovedDownByRows(start, rowsDragged, moveTime);
    pane.view->beginTouchScroll(start);
    pane.view->moveTouchScroll(moved);
    pane.view->endTouchScroll(touchAt(moved.pos, liftTime));
    pane.showOutput(makeLinesOfOutput(1));

    check(pane.view->countLinesArrivedWhileScrolledBack() == 1);
    check(pane.takeWritten().empty(), "scrolling the scrollback sends the shell nothing");

    pane.view->scrollToLiveBottom();
    check(pane.view->countLinesArrivedWhileScrolledBack() == 0);
};

auto tTouchDragInAltScreen = test("View/aFingerDragInAltScreenSendsOneArrowPerLine") = []
{
    constexpr auto rowsDragged = 3.f;
    constexpr auto moveTime = 0.02;
    constexpr auto liftTime = 0.03;
    auto pane = TestPane {};
    pane.showOutput("\033[?1049h");
    pane.takeWritten();

    const auto start = touchAt(centreOfCell(0, 0), 0.0);
    const auto moved = touchMovedDownByRows(start, rowsDragged, moveTime);
    pane.view->beginTouchScroll(start);
    pane.view->moveTouchScroll(moved);
    check(pane.takeWritten() == "\033[A\033[A\033[A");

    pane.view->endTouchScroll(touchAt(moved.pos, liftTime));
    check(pane.takeWritten().empty(), "no coast follows the finger in a program");
};

auto tTouchDragUnderMouseReporting = test("View/aFingerDragUnderTmuxMouseSendsWheelReportsNotAPress") = []
{
    constexpr auto rowsDragged = 2.f;
    constexpr auto moveTime = 0.02;
    constexpr auto liftTime = 0.03;
    constexpr auto wheelUpReportUnderTheFinger = "\033[<64;1;3M";
    auto pane = TestPane {};
    pane.showOutput("\033[?1049h\033[?1002h\033[?1006h");
    pane.takeWritten();

    const auto start = touchAt(centreOfCell(0, 0), 0.0);
    const auto moved = touchMovedDownByRows(start, rowsDragged, moveTime);
    pane.view->beginTouchScroll(start);
    pane.view->moveTouchScroll(moved);
    pane.view->endTouchScroll(touchAt(moved.pos, liftTime));

    const auto written = pane.takeWritten();
    check(written == std::string {wheelUpReportUnderTheFinger} + wheelUpReportUnderTheFinger,
          "a finger dragged down two rows scrolls the program back two lines");
    check(written.find("\033[<0;") == std::string::npos, "never a button press, which starts tmux's selection");
};

auto tScrolledBackStateReported = test("View/leavingAndReturningToTheBottomIsReported") = []
{
    constexpr auto linesOfHistory = 50;
    constexpr auto linesArrivingLater = 2;
    constexpr auto wheelLinesBack = 5.f;
    auto pane = TestPane {};
    pane.showOutput(makeLinesOfOutput(linesOfHistory));
    auto reports = std::vector<ScrolledBackState> {};
    pane.view->onScrolledBackStateChanged = [&reports](ScrolledBackState state)
    { reports.push_back(state); };

    pane.view->mouseWheel(wheelBy(wheelLinesBack));
    check(reports.size() == 1 && reports.back().isScrolledBack);

    pane.showOutput(makeLinesOfOutput(linesArrivingLater));
    check(reports.back().linesArrivedWhileScrolledBack == linesArrivingLater);

    pane.view->scrollToLiveBottom();
    check(!reports.back().isScrolledBack);
    check(reports.back().linesArrivedWhileScrolledBack == 0);
};

namespace
{
constexpr auto fixedColumns = 30;
constexpr auto fixedRows = 3;

int countRowsInTestView()
{
    const auto heightForRows = viewHeight - 2.f * marginY;
    const auto rowsThatFit = std::floor(heightForRows / referenceAtlas().cellHeight());
    const auto rows = static_cast<int>(rowsThatFit);
    return rows;
}
} // namespace

auto tFixedGridSize = test("View/aFixedGridIsWhatTheShellGetsWhateverTheBounds") = []
{
    constexpr auto tallerHeight = 300.f;
    auto pane = TestPane {};
    pane.view->setFixedGridSize(PtySize {fixedColumns, fixedRows});

    check(pane.recording->resizes.back().cols == fixedColumns);
    check(pane.recording->resizes.back().rows == fixedRows);

    const auto resizesBefore = pane.recording->resizes.size();
    pane.view->setBounds({0.f, 0.f, viewWidth, tallerHeight});
    pane.view->resized();
    check(pane.recording->resizes.size() == resizesBefore,
          "the keyboard going away resizes nothing");
};

auto tFixedGridMouseRows = test("View/belowScrollbackRowsTheGridRowsReportFromOne") = []
{
    auto pane = TestPane {};
    pane.view->setFixedGridSize(PtySize {fixedColumns, fixedRows});
    pane.showOutput("\033[?1000h\033[?1006h");
    pane.takeWritten();

    const auto bottomRow = countRowsInTestView() - 1;
    const auto firstGridRow = countRowsInTestView() - fixedRows;
    pane.view->mouseDown(mouseAtCell(0, bottomRow));
    check(pane.takeWritten() == "\033[<0;1;3M", "the view's last row is the grid's last");

    pane.view->mouseDown(mouseAtCell(0, firstGridRow));
    check(pane.takeWritten() == "\033[<0;1;1M");
};

// ---- copy mode keys ---------------------------------------------------------

auto tCopyModeSwallowsKeys = test("View/copyModeKeepsKeysFromTheShell") = []
{
    auto pane = TestPane {};
    pane.view->enterCopyMode();
    check(pane.view->inCopyMode());

    check(pane.writtenForKey(typedKey("x")).empty());
    check(pane.writtenForKey(typedKey("u", controlHeld())).empty());
    check(pane.writtenForKey(keyWithCode(Graphics::KeyCode::F5)).empty());
    check(pane.view->inCopyMode());

    check(pane.writtenForKey(typedKey("k", commandHeld())) == "\014",
          "command shortcuts stay live");
};

auto tCopyModeExits = test("View/copyModeLeavesOnQControlCYankAndEscape") = []
{
    auto pane = TestPane {};

    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("q"));
    check(!pane.view->inCopyMode());

    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("c", controlHeld()));
    check(!pane.view->inCopyMode());

    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("y"));
    check(!pane.view->inCopyMode());

    pane.view->enterCopyMode();
    pane.view->keyDown(keyWithCode(Graphics::KeyCode::Return));
    check(!pane.view->inCopyMode());

    pane.view->enterCopyMode();
    pane.view->keyDown(keyWithCode(macKeypadEnter));
    check(!pane.view->inCopyMode());

    pane.view->enterCopyMode();
    pane.view->keyDown(keyWithCode(Graphics::KeyCode::Escape));
    check(!pane.view->inCopyMode());
};

auto tEscapeDropsSelectionFirst = test("View/copyModeEscapeDropsTheSelectionFirst") = []
{
    auto pane = TestPane {};
    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("v"));

    pane.view->keyDown(keyWithCode(Graphics::KeyCode::Escape));
    check(pane.view->inCopyMode());

    pane.view->keyDown(keyWithCode(Graphics::KeyCode::Escape));
    check(!pane.view->inCopyMode());
};

// ---- pixels: selection, copy mode, attributes -------------------------------

namespace
{
constexpr auto sampleText = "aa bb cc\r\nee ff\r\n";
} // namespace

auto tCopyCursorStartsAtTheTerminalCursor = test("View/copyCursorStartsAtTheCursor") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);
    pane.view->enterCopyMode();

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 0, 2, defaultTheme.cursor));
    check(isCellPainted(image, 1, 2, defaultTheme.background));
};

auto tCopyModeMotions = test("View/copyModeMotionsMoveTheBlock") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);
    pane.view->enterCopyMode();

    pane.view->keyDown(typedKey("k"));
    pane.view->keyDown(typedKey("k"));
    pane.view->keyDown(typedKey("w"));
    auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 3, 0, defaultTheme.cursor), "w lands on bb");

    pane.view->keyDown(typedKey("w"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 6, 0, defaultTheme.cursor), "w again lands on cc");

    pane.view->keyDown(typedKey("w"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 0, 1, defaultTheme.cursor), "w wraps to the next line");

    pane.view->keyDown(typedKey("b"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 6, 0, defaultTheme.cursor), "b goes back over the wrap");

    pane.view->keyDown(typedKey("b"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 3, 0, defaultTheme.cursor));

    pane.view->keyDown(typedKey("$"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 7, 0, defaultTheme.cursor), "$ ends at the last ink");

    pane.view->keyDown(typedKey("0"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 0, 0, defaultTheme.cursor));

    pane.view->keyDown(typedKey("l"));
    pane.view->keyDown(typedKey("j"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 1, 1, defaultTheme.cursor), "l then j");

    pane.view->keyDown(keyWithCode(Graphics::KeyCode::LeftArrow));
    pane.view->keyDown(keyWithCode(Graphics::KeyCode::UpArrow));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 0, 0, defaultTheme.cursor), "arrows move too");

    pane.view->keyDown(typedKey("h"));
    pane.view->keyDown(typedKey("k"));
    image = pane.view->renderToImage(1.f);
    check(isCellPainted(image, 0, 0, defaultTheme.cursor), "clamped at the top-left");
};

auto tCopyModeSelection = test("View/copyModeSelectsFromTheAnchor") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);
    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("k"));
    pane.view->keyDown(typedKey("k"));
    pane.view->keyDown(typedKey("w"));
    pane.view->keyDown(typedKey("v"));
    pane.view->keyDown(typedKey("w"));

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 2, 0, defaultTheme.background));
    check(isCellPainted(image, 3, 0, defaultTheme.selection));
    check(isCellPainted(image, 5, 0, defaultTheme.selection));
    check(isCellPainted(image, 6, 0, defaultTheme.cursor));
    check(isCellPainted(image, 7, 0, defaultTheme.background));
};

auto tCopyModeLineSelection = test("View/copyModeLineSelectionCoversWholeRows") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);
    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("k"));
    pane.view->keyDown(typedKey("V"));

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 3, 1, defaultTheme.selection));
    check(isCellPainted(image, 20, 1, defaultTheme.selection));
    check(isCellPainted(image, 3, 0, defaultTheme.background));
};

auto tCopyModeBadge = test("View/copyModeShowsItsPositionBadge") = []
{
    auto pane = TestPane {};
    pane.view->enterCopyMode();

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    const auto& atlas = referenceAtlas();
    const auto badgeTop = (int) std::ceil(marginY + samplingInsetInPoints);
    const auto badgeRight = (int) (viewWidth - marginX - 2.f);
    check(matches(image.at(badgeRight, badgeTop), defaultTheme.ansi[3]));

    const auto belowBadge = (int) (marginY + atlas.cellHeight() + 2.f);
    check(matches(image.at(badgeRight, belowBadge), defaultTheme.background));

    pane.view->keyDown(typedKey("q"));
    const auto afterExit = pane.view->renderToImage(1.f);
    check(matches(afterExit.at(badgeRight, badgeTop), defaultTheme.background));
};

auto tDoubleClickSelectsWord = test("View/doubleClickSelectsAWord") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);

    auto event = mouseAtCell(4, 0);
    event.clickCount = 2;
    pane.view->mouseDown(event);
    pane.view->mouseUp(event);

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 2, 0, defaultTheme.background));
    check(isCellPainted(image, 3, 0, defaultTheme.selection));
    check(isCellPainted(image, 4, 0, defaultTheme.selection));
    check(isCellPainted(image, 5, 0, defaultTheme.background));
};

auto tDoubleClickOnBlankSelectsNothing = test("View/doubleClickOnABlankSelectsNothing") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);

    auto event = mouseAtCell(2, 0);
    event.clickCount = 2;
    pane.view->mouseDown(event);
    pane.view->mouseUp(event);

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 1, 0, defaultTheme.background));
    check(isCellPainted(image, 2, 0, defaultTheme.background));
    check(isCellPainted(image, 3, 0, defaultTheme.background));
};

auto tTripleClickSelectsLine = test("View/tripleClickSelectsTheRow") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);

    auto event = mouseAtCell(1, 1);
    event.clickCount = 3;
    pane.view->mouseDown(event);
    pane.view->mouseUp(event);

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 0, 1, defaultTheme.selection));
    check(isCellPainted(image, 20, 1, defaultTheme.selection));
    check(isCellPainted(image, 0, 0, defaultTheme.background));
};

auto tDragSelects = test("View/dragSelectsFromTheAnchor") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);

    pane.view->mouseDown(mouseAtCell(4, 0));
    pane.view->mouseDragged(mouseAtCell(1, 1));
    pane.view->mouseUp(mouseAtCell(1, 1));

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 3, 0, defaultTheme.background));
    check(isCellPainted(image, 4, 0, defaultTheme.selection));
    check(isCellPainted(image, 20, 0, defaultTheme.selection));
    check(isCellPainted(image, 0, 1, defaultTheme.selection));
    check(isCellPainted(image, 1, 1, defaultTheme.selection));
    check(isCellPainted(image, 2, 1, defaultTheme.background));
};

auto tClickWithoutDragClears = test("View/aPlainClickLeavesNoSelection") = []
{
    auto pane = TestPane {};
    pane.showOutput(sampleText);

    auto event = mouseAtCell(4, 0);
    event.clickCount = 3;
    pane.view->mouseDown(event);
    pane.view->mouseUp(event);

    pane.view->mouseDown(mouseAtCell(4, 0));
    pane.view->mouseUp(mouseAtCell(4, 0));

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 4, 0, defaultTheme.background));
    check(isCellPainted(image, 0, 0, defaultTheme.background));
};

auto tInverseVideo = test("View/inverseVideoPaintsTheForegroundBehind") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[7maa\033[0m bb\r\n");

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isCellPainted(image, 0, 0, defaultTheme.foreground));
    check(isCellPainted(image, 1, 0, defaultTheme.foreground));
    check(isCellPainted(image, 2, 0, defaultTheme.background));
};

auto tUnfocusedCursorIsHollow = test("View/anUnfocusedCursorIsAnOutline") = []
{
    auto pane = TestPane {};
    pane.showOutput("aa");

    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    const auto centre = centreOfCell(2, 0);
    check(matches(image.at((int) centre.x, (int) centre.y), defaultTheme.background),
          "an unfocused pane draws an outline, not a block");
};

namespace
{
Graphics::MouseEvent linkClickAtCell(int col, int row)
{
    auto event = mouseAtCell(col, row);

    if constexpr (Platform::isMac())
        event.modifiers.command = true;
    else
        event.modifiers.control = true;

    return event;
}

struct LinkPane
{
    LinkPane()
    {
        pane.view->onOpenHyperlink = [this](const std::string& uri) { opened = uri; };
    }

    TestPane pane;
    std::string opened;
};
} // namespace

auto tLinkClickOpensTheHyperlink = test("View/linkClickOpensTheHyperlink") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("\033]8;;https://example.com\033\\link\033]8;;\033\\ plain");

    linked.pane.view->mouseDown(linkClickAtCell(1, 0));
    check(linked.opened == "https://example.com");

    linked.opened.clear();
    linked.pane.view->mouseDown(linkClickAtCell(6, 0));
    check(linked.opened.empty(), "text after the link's end opens nothing");
};

auto tLinkClickOpensAPlainTextUrl = test("View/linkClickOpensAUrlWrittenAsText") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("see https://example.com/a. done");

    linked.pane.view->mouseDown(linkClickAtCell(6, 0));
    check(linked.opened == "https://example.com/a");
};

namespace
{
constexpr auto underlineSearchRows = 4;

// Whether any pixel in the strip under a cell's baseline, where an underline
// sits and lowercase letters leave background, has the colour.
bool isUnderlinePainted(const Graphics::Image& image, int col, int row, Rgb expected)
{
    const auto& atlas = referenceAtlas();
    const auto x = marginX + ((float) col + 0.5f) * atlas.cellWidth();
    const auto baselineY = marginY + (float) row * atlas.cellHeight() + atlas.baseline();
    const auto firstY = (int) std::floor(baselineY) + 1;

    for (auto y = firstY; y < firstY + underlineSearchRows; ++y)
    {
        if (matches(image.at((int) x, y), expected))
            return true;
    }

    return false;
}
} // namespace

auto tHoverUnderlinesTheLink = test("View/linkModifierHoverUnderlinesTheWholeLink") = []
{
    auto pane = TestPane {};
    pane.showOutput("x https://example.com/a y");
    constexpr auto hoveredColumn = 5;
    constexpr auto otherLinkColumn = 15;
    constexpr auto plainColumn = 24;

    pane.view->mouseMoved(linkClickAtCell(hoveredColumn, 0));
    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isUnderlinePainted(image, otherLinkColumn, 0, defaultTheme.foreground));
    check(!isUnderlinePainted(image, plainColumn, 0, defaultTheme.foreground));

    pane.view->mouseMoved(mouseAtCell(hoveredColumn, 0));
    const auto released = pane.view->renderToImage(1.f);
    check(!isUnderlinePainted(released, otherLinkColumn, 0, defaultTheme.foreground),
          "without the modifier the underline goes");
};

auto tPlainClickOnALinkSelects = test("View/aPlainClickOnALinkOpensNothing") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("\033]8;;https://example.com\033\\link\033]8;;\033\\");

    linked.pane.view->mouseDown(mouseAtCell(1, 0));

    check(linked.opened.empty());
};

auto tCustomSchemesAreNotOpened = test("View/linkWithACustomSchemeIsNotOpened") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("\033]8;;x-app://run\033\\link\033]8;;\033\\");

    linked.pane.view->mouseDown(linkClickAtCell(1, 0));

    check(linked.opened.empty());
};

namespace
{
// An arrow as macOS delivers it: the key code plus a Private Use Area
// character (U+F700 up, U+F701 down, U+F702 left, U+F703 right).
Graphics::KeyEvent macArrow(std::uint16_t keyCode, std::string puaCharacter)
{
    auto event = keyWithCode(keyCode);
    event.characters = puaCharacter;
    event.charactersIgnoringModifiers = std::move(puaCharacter);
    return event;
}

const auto macUp = std::string {"\xef\x9c\x80"};
const auto macLeft = std::string {"\xef\x9c\x82"};
} // namespace

auto tMacArrowsLegacy = test("View/macArrowsWithTheirCharactersSendCsi") = []
{
    auto pane = TestPane {};
    check(pane.writtenForKey(macArrow(Graphics::KeyCode::UpArrow, macUp)) == "\033[A");
    check(pane.writtenForKey(macArrow(Graphics::KeyCode::LeftArrow, macLeft)) == "\033[D");
};

auto tMacArrowsApplicationMode = test("View/macArrowsInApplicationCursorModeSendSs3") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1h");
    check(pane.writtenForKey(macArrow(Graphics::KeyCode::UpArrow, macUp)) == "\033OA");
    check(pane.writtenForKey(macArrow(Graphics::KeyCode::LeftArrow, macLeft)) == "\033OD");
};

auto tMacArrowsKitty = test("View/macArrowsUnderKittyFlagsSendCsi") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[>5u");
    check(pane.writtenForKey(macArrow(Graphics::KeyCode::UpArrow, macUp)) == "\033[A");
    pane.showOutput("\033[?1h");
    check(pane.writtenForKey(macArrow(Graphics::KeyCode::UpArrow, macUp)) == "\033[A");
};

auto tPaneOutsideAWindowIsNotShown = test("View/aPaneOutsideAWindowIsNotShown") = []
{
    auto pane = TestPane {};
    pane.showOutput("hello");

    check(!pane.view->isShownOnScreen());
};

auto tPaneWithNoSizeIsNotShown = test("View/aPaneWithNoSizeIsNotShown") = []
{
    auto pane = TestPane {};
    pane.view->setBounds({});

    check(!pane.view->isShownOnScreen());
};

auto tDraggedResizesCoalesce = test("View/resizesWhileDraggingReachTheShellOnceWithTheFinalSize") = []
{
    constexpr auto dragStepWidth = 40.f;
    constexpr auto dragSteps = 4;
    constexpr auto settleTimeout = Time::MS {2000};

    auto pane = TestPane {};
    pane.recording->resizes.clear();

    for (int step = 1; step <= dragSteps; ++step)
    {
        const auto width = viewWidth + dragStepWidth * (float) step;
        pane.view->setBounds({0.f, 0.f, width, viewHeight});
        pane.view->resized();
    }

    check(pane.recording->resizes.empty());

    Threads::runEventLoopUntil([&] { return !pane.recording->resizes.empty(); },
                               settleTimeout);

    const auto finalWidth = viewWidth + dragStepWidth * (float) dragSteps;
    const auto expectedColumns =
        (int) ((finalWidth - 2 * marginX) / referenceAtlas().cellWidth());

    check(pane.recording->resizes.size() == 1);
    check(pane.recording->resizes.back().cols == expectedColumns);
};

// A pane attached to a shell that already runs elsewhere (a remote daemon, or
// the local one after CowTerm restarts) learns the program's mouse mode only
// from the restored state, never from fresh output.
auto tRestoredMouseModeReportsTheWheel = test("View/aRestoredMouseModeReportsTheWheel") = []
{
    constexpr auto daemonColumns = 80;
    constexpr auto daemonRows = 24;
    auto daemonSide = TerminalModel {daemonColumns, daemonRows, Theme {}};
    daemonSide.feed("\033[?1049h\033[?1000h\033[?1006h");

    auto pane = TestPane {};
    pane.recording->deliverRestoredState(serializeTerminalState(daemonSide.captureState()));
    pane.showOutput({});
    pane.takeWritten();

    pane.view->mouseWheel(wheelBy(1.f));
    check(pane.takeWritten() == "\033[<64;1;1M", "the wheel goes to the program, not the scrollback");
};

// ---- selecting and copying text ----------------------------------------------

namespace
{
// More lines than the scrollback keeps, so each new line drops the oldest.
constexpr auto linesBeyondTheScrollbackLimit = 10050;
constexpr auto linesArrivingLater = 3;
constexpr auto linesOfHistory = 100;
constexpr auto linesScrolledBack = 20.f;

std::string makeNumberedLines(int first, int count)
{
    auto lines = std::string {};

    for (auto number = first; number < first + count; ++number)
        lines += "line " + std::to_string(number) + "\r\n";

    return lines;
}

int countVisibleColumns()
{
    return (int) ((viewWidth - 2 * marginX) / referenceAtlas().cellWidth());
}

std::string makeLineWrappingOntoASecondRow()
{
    constexpr auto columnsOnTheSecondRow = 5;
    return std::string ((std::size_t) (countVisibleColumns() + columnsOnTheSecondRow), 'x');
}

Graphics::MouseEvent shiftMouseAtCell(int col, int row)
{
    auto event = mouseAtCell(col, row);
    event.modifiers = shiftHeld();
    return event;
}

Graphics::MouseEvent clicksAtCell(int col, int row, int clickCount)
{
    auto event = mouseAtCell(col, row);
    event.clickCount = clickCount;
    return event;
}

constexpr auto doubleClick = 2;
constexpr auto tripleClick = 3;
} // namespace

auto tShiftReleasedMidDrag = test("Select/releasingShiftMidDragKeepsSelectingAndTellsTheProgramNothing") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1002h\033[?1006h0123456789");
    pane.takeWritten();

    pane.view->mouseDown(shiftMouseAtCell(1, 0));
    pane.view->mouseDragged(shiftMouseAtCell(3, 0));
    pane.view->mouseDragged(mouseAtCell(5, 0));
    pane.view->mouseUp(mouseAtCell(5, 0));

    check(pane.takeWritten().empty(), "the program never saw the press, so it gets no drag or release");
    check(pane.view->getSelectedText() == "12345");
};

auto tShiftPressedMidDrag = test("Select/aPressTheProgramGotIsReleasedToItEvenWithShiftHeld") = []
{
    auto pane = TestPane {};
    pane.showOutput("\033[?1002h\033[?1006h0123456789");
    pane.takeWritten();

    pane.view->mouseDown(mouseAtCell(1, 0));
    pane.view->mouseDragged(shiftMouseAtCell(3, 0));
    pane.view->mouseUp(shiftMouseAtCell(3, 0));

    check(pane.takeWritten() == "\033[<0;2;1M\033[<32;4;1M\033[<0;4;1m");
    check(pane.view->getSelectedText().empty());
};

auto tSingleCharacterWord = test("Select/doubleClickOnAOneLetterWordCopiesIt") = []
{
    auto pane = TestPane {};
    pane.showOutput("a b c");

    pane.view->mouseDown(clicksAtCell(2, 0, doubleClick));
    pane.view->mouseUp(clicksAtCell(2, 0, doubleClick));

    check(pane.view->getSelectedText() == "b");
};

auto tWordAcrossSoftWrap = test("Select/doubleClickTakesAWordTheTerminalWrapped") = []
{
    auto pane = TestPane {};
    constexpr auto wordColumnsOnTheFirstRow = 4;
    const auto padding =
        std::string ((std::size_t) (countVisibleColumns() - wordColumnsOnTheFirstRow), ' ');
    const auto onTheFirstRow = countVisibleColumns() - wordColumnsOnTheFirstRow / 2;
    pane.showOutput(padding + "/usr/local/bin done");

    pane.view->mouseDown(clicksAtCell(2, 1, doubleClick));
    pane.view->mouseUp(clicksAtCell(2, 1, doubleClick));
    check(pane.view->getSelectedText() == "/usr/local/bin", "from the second row");

    pane.view->mouseDown(clicksAtCell(onTheFirstRow, 0, doubleClick));
    pane.view->mouseUp(clicksAtCell(onTheFirstRow, 0, doubleClick));
    check(pane.view->getSelectedText() == "/usr/local/bin", "from the first row");
};

auto tRowAcrossSoftWrap = test("Select/tripleClickTakesTheWholeWrappedLine") = []
{
    auto pane = TestPane {};
    const auto longLine = makeLineWrappingOntoASecondRow();
    pane.showOutput("before\r\n" + longLine + "\r\nafter");

    pane.view->mouseDown(clicksAtCell(1, 2, tripleClick));
    pane.view->mouseUp(clicksAtCell(1, 2, tripleClick));

    check(pane.view->getSelectedText() == longLine);
};

auto tSelectionFollowsDroppedLines = test("Select/aSelectionStaysOnItsTextWhenTheFullScrollbackDropsLines") = []
{
    auto pane = TestPane {};
    pane.showOutput(makeNumberedLines(0, linesBeyondTheScrollbackLimit));

    pane.view->mouseDown(mouseAtCell(0, 0));
    pane.view->mouseDragged(mouseAtCell(3, 1));
    pane.view->mouseUp(mouseAtCell(3, 1));
    const auto selected = pane.view->getSelectedText();

    pane.showOutput(makeNumberedLines(linesBeyondTheScrollbackLimit, linesArrivingLater));

    check(pane.view->getSelectedText() == selected);
};

auto tClearedScrollbackDropsSelection = test("Select/clearingTheScrollbackDropsTheSelection") = []
{
    auto pane = TestPane {};
    pane.showOutput(makeNumberedLines(0, linesOfHistory));
    pane.view->mouseWheel(wheelBy((float) linesOfHistory));

    pane.view->mouseDown(mouseAtCell(0, 0));
    pane.view->mouseDragged(mouseAtCell(6, 0));
    pane.view->mouseUp(mouseAtCell(6, 0));
    check(pane.view->getSelectedText() == "line 0");

    pane.showOutput("\033[3J\033[Hredrawn by the program");

    check(pane.view->getSelectedText().empty(), "the rows it named now hold other text");
};

auto tDragInScrollbackWhileOutputArrives = test("Select/draggingInTheScrollbackHoldsTheViewStillAsOutputArrives") = []
{
    auto pane = TestPane {};
    pane.showOutput(makeNumberedLines(0, linesOfHistory));
    pane.view->mouseWheel(wheelBy(linesScrolledBack));

    pane.view->mouseDown(mouseAtCell(0, 0));
    pane.view->mouseDragged(mouseAtCell(5, 0));
    const auto underThePointer = pane.view->getSelectedText();

    pane.showOutput(makeNumberedLines(linesOfHistory, linesArrivingLater));
    pane.view->mouseDragged(mouseAtCell(5, 0));
    pane.view->mouseUp(mouseAtCell(5, 0));

    check(pane.view->getSelectedText() == underThePointer);
};

auto tCopyModeSelectionFollowsDroppedLines = test("Select/aCopyModeSelectionStaysOnItsTextWhenTheFullScrollbackDropsLines") = []
{
    auto pane = TestPane {};
    pane.showOutput(makeNumberedLines(0, linesBeyondTheScrollbackLimit));
    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("k"));
    pane.view->keyDown(typedKey("V"));
    const auto selected = pane.view->getSelectedText();

    pane.showOutput(makeNumberedLines(linesBeyondTheScrollbackLimit, linesArrivingLater));

    check(selected == "line " + std::to_string(linesBeyondTheScrollbackLimit - 1));
    check(pane.view->getSelectedText() == selected);
};

auto tRewrapDropsSelection = test("Select/rewrappingForANewWidthDropsTheSelection") = []
{
    constexpr auto narrowerBy = 100.f;
    constexpr auto settleTimeout = Time::MS {2000};

    auto pane = TestPane {};
    constexpr auto wrappedRows = 2;
    pane.showOutput(std::string ((std::size_t) (countVisibleColumns() * wrappedRows), 'x')
                    + "\r\nend");
    pane.view->mouseDown(clicksAtCell(0, wrappedRows, tripleClick));
    pane.view->mouseUp(clicksAtCell(0, wrappedRows, tripleClick));
    check(pane.view->getSelectedText() == "end");

    pane.recording->resizes.clear();
    pane.view->setBounds({0.f, 0.f, viewWidth - narrowerBy, viewHeight});
    pane.view->resized();
    Threads::runEventLoopUntil([&] { return !pane.recording->resizes.empty(); },
                               settleTimeout);

    check(pane.view->getSelectedText().empty(), "the rows it named now hold other text");
};

// ---- copy mode: what y copies -------------------------------------------------

auto tCopyModeStaysInAFullScreenProgram = test("CopyMode/inAFullScreenProgramTheTopIsTheProgramsTopRow") = []
{
    auto pane = TestPane {};
    pane.showOutput(makeNumberedLines(0, linesOfHistory));
    pane.showOutput("\033[?1049h\033[Happ top row\r\napp second row");

    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("g"));
    pane.view->keyDown(typedKey("V"));

    check(pane.view->getSelectedText() == "app top row",
          "the shell's history behind a full-screen program is not on its screen");
};

auto tCopyModeCrossesTheWrap = test("CopyMode/dollarAndLineSelectionReachTheEndOfAWrappedLine") = []
{
    auto pane = TestPane {};
    const auto longLine = makeLineWrappingOntoASecondRow();
    pane.showOutput(longLine + "\r\nnext");

    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("g"));
    pane.view->keyDown(typedKey("v"));
    pane.view->keyDown(typedKey("$"));
    check(pane.view->getSelectedText() == longLine, "$ from the first row");

    pane.view->keyDown(keyWithCode(Graphics::KeyCode::Escape));
    pane.view->keyDown(typedKey("V"));
    check(pane.view->getSelectedText() == longLine, "V on the second row");
};

auto tCopyModeReattach = test("CopyMode/aReattachDropsTheSelectionInsteadOfCopyingOtherRows") = []
{
    const auto visibleRows = (int) ((viewHeight - 2 * marginY) / referenceAtlas().cellHeight());
    auto daemonSide = TerminalModel {countVisibleColumns(), visibleRows, Theme {}};
    daemonSide.feed(makeNumberedLines(0, linesOfHistory));

    auto pane = TestPane {};
    pane.showOutput(makeNumberedLines(0, linesOfHistory / 2));
    pane.view->enterCopyMode();
    pane.view->keyDown(typedKey("k"));
    pane.view->keyDown(typedKey("V"));
    check(!pane.view->getSelectedText().empty());

    pane.recording->deliverRestoredState(serializeTerminalState(daemonSide.captureState()));

    check(pane.view->inCopyMode());
    check(pane.view->getSelectedText().empty());
};

auto tHeldPaneDrawsTheHoldersGrid = test("View/aPaneAPhoneHoldsIsDrawnAtThePhonesGrid") = []
{
    constexpr auto phoneColumns = 45;
    constexpr auto phoneRows = 17;
    auto pane = TestPane {};
    const auto deskGrid = pane.recording->resizes.back();

    pane.recording->setGridHeldElsewhere(PtySize {phoneColumns, phoneRows});
    check(pane.recording->resizes.back().cols == phoneColumns);

    pane.recording->setGridHeldElsewhere(std::nullopt);
    check(pane.recording->resizes.back().cols == deskGrid.cols, "the desk's grid when the phone lets go");
};

auto tClickOnTheNoteTakesTheSizeBack = test("View/aClickOnTheOnIPhoneNoteTakesThePaneBack") = []
{
    constexpr auto noteText = "on iPhone, 45 columns; click here to take it back";
    const auto pointInsideTheNote = Graphics::Point {viewWidth - marginX - 2.f, marginY + 2.f};
    auto pane = TestPane {};
    pane.recording->showStatus(noteText);
    pane.takeWritten();

    auto click = Graphics::MouseEvent {};
    click.pos = pointInsideTheNote;
    click.downPos = pointInsideTheNote;
    click.button = Graphics::MouseButton::Left;
    pane.view->mouseDown(click);

    check(pane.recording->sizeTakeBacks == 1);
    check(pane.takeWritten().empty(), "the click reaches no program");
};
