#include <CowTermTerminal/TerminalView.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <memory>
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
    std::function<void(std::string)> deliverOutput;
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
        return true;
    }

    void write(std::string_view data) override { recording->written += data; }
    void resize(const PtySize& size) override { recording->resizes.push_back(size); }
    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }
    void terminate() override {}
    void detach() override {}

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
