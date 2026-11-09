#pragma once

#include "CopyMode.h"
#include "GlyphAtlas.h"
#include "GridPainter.h"
#include "GridSelection.h"
#include "KeyEncoding.h"
#include "KittyKeyEncoding.h"
#include "OutputParserThread.h"

#include "CowTermCore/FocusReports.h"
#include "CowTermCore/Shell.h"
#include "CowTermCore/TermParser.h"
#include "CowTermCore/TermScreen.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Threads/Timer.h>
#include <eacp/Sprites/Sprites.h>

#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <optional>

namespace term
{
inline constexpr float minFontSize = 7.0f;
inline constexpr float maxFontSize = 40.0f;

// Everything the widget takes from its host. A consumer fills the
// presentation fields and, when shells should outlive the widget, replaces
// makeShell — the default runs the shell in-process, so an embedded
// terminal works with no daemon at all. The CowTerm app passes its
// daemon-backed factory (terminalConfig in DaemonClient.h).
struct TerminalConfig
{
    std::string font = "JetBrains Mono";
    float fontSize = 13.0f;
    Theme theme;

    // Builds the shell behind an interactive pane, keyed by its stable id.
    // Command panes (a non-empty commandToRun) always run in-process.
    std::function<std::unique_ptr<Shell>(const std::string& shellId)> makeShell =
        [](const std::string&) { return std::make_unique<LocalShell>(); };

    // Extra environment for an interactive pane's shell, keyed by its stable
    // id. Command panes get none.
    std::function<std::vector<EnvironmentVariable>(const std::string& shellId)>
        environmentForShell = [](const std::string&)
    { return std::vector<EnvironmentVariable> {}; };
};

// The terminal: a GPU-rendered cell grid over a live PTY. Output is parsed
// into the screen model and drawn from a glyph atlas; every visible pixel is
// composited on the GPU each frame (backgrounds, glyphs, decorations,
// selection, cursor). Repaints are on-demand: PTY output, input, blink.
class TerminalView final : public eacp::GPU::GPUView
{
public:
    // shellId names this pane's shell in the session daemon; panes restored
    // from a snapshot pass their saved id so a still-running shell is
    // adopted. Empty generates a fresh one. A non-empty command runs that
    // command instead of an interactive shell, in an in-process PTY the
    // daemon never sees — the terminal is ephemeral by construction and
    // ends with the command (the lazygit popup).
    TerminalView(const TerminalConfig& config,
                 const std::string& workingDirectory,
                 const std::string& shellIdToUse = {},
                 const std::string& commandToRun = {});
    ~TerminalView() override;

    const std::string& shellId() const { return paneShellId; }

    // Debug tracing: is this the ephemeral command terminal (the popup)?
    bool isCommandTerminal() const { return commandTerminal; }

    // Debug tracing: the visible grid as plain text, blanks trimmed.
    std::string debugScreenText() const;

    // Blocks until every byte the shell has sent so far is on the screen; its
    // repaint is then the next thing queued for the UI thread. For tests.
    void waitForParsedOutput();

    // Ends the shell process on purpose (pane closed). The destructor only
    // detaches, so shells held by the daemon survive teardown.
    void terminateShell();

    // Types text into the shell as if the user had; "\n" presses Enter.
    // Backs the config file's send-keys bindings.
    void sendText(std::string_view text);

    // Focus moved into or out of this pane: redraw the cursor in its new
    // state now instead of waiting for the next blink tick.
    void refreshCursor();

    // The window gained or lost key focus. With refreshCursor's pane changes,
    // this drives mode 1004's focus reports (CSI I / CSI O).
    void setWindowFocused(bool focused);

    // Zoom is app-wide, not per pane: the shell owns the shortcut, persists
    // the size in the config and pushes it into every live pane through here.
    void setFontSize(float newSize);

    // tmux copy mode (Ctrl+A [): keyboard scrollback navigation with vi
    // keys. h/j/k/l and friends move, v/V select, y yanks to the clipboard
    // and leaves, q leaves. While active, keys never reach the shell.
    void enterCopyMode();
    bool inCopyMode() const { return copyMode; }

    void paste();

    std::function<void(const std::string&)> onTitleChanged =
        [](const std::string&) {};
    std::function<void(const std::string&)> onCwdChanged = [](const std::string&) {};
    std::function<void(const std::string&)> onNotify = [](const std::string&) {};
    eacp::Callback onShellExit = [] {};

    // Cmd-click (Ctrl-click off macOS) on an OSC 8 hyperlink; the host opens it.
    std::function<void(const std::string&)> onOpenHyperlink = [](const std::string&) {};

    // Fired when the user clicks into this pane; the session view moves its
    // active-pane marker here.
    eacp::Callback onFocused = [] {};

    // Runs before any terminal key handling; return true to consume the
    // event. The app shell hangs its leader-key table and global shortcuts
    // here.
    std::function<bool(const eacp::Graphics::KeyEvent&)> interceptKey =
        [](const eacp::Graphics::KeyEvent&) { return false; };

    const std::string& currentCwd() const { return cwd; }
    const std::string& currentTitle() const { return title; }
    std::string foregroundProcess() const { return shell->foregroundProcess(); }

    // Best known working directory: OSC 7 when the shell reports it, else
    // the kernel's answer for the shell process.
    std::string workingDirectory() const
    {
        if (!cwd.empty())
            return cwd;

        return shell->currentWorkingDirectory();
    }

    void render(eacp::GPU::Frame& frame) override;
    void resized() override;

    void keyDown(const eacp::Graphics::KeyEvent& event) override;
    void keyUp(const eacp::Graphics::KeyEvent& event) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;
    void mouseWheel(const eacp::Graphics::MouseEvent& event) override;

private:
    void connectParserToUiThread();
    void startOutputParserThread();
    std::unique_lock<std::recursive_mutex> lockTerminal() const;
    void runOnUiThread(std::function<void()> action);
    void requestShowParsedOutput();
    void showParsedOutput();
    void reportFocusIfChanged();
    bool openHyperlinkUnderPointer(const eacp::Graphics::MouseEvent& event);
    void repaintUnlessSynchronized();
    void startSynchronizedOutputTimeout();
    void send(std::string_view bytes);
    void sendAndScrollToBottom(std::string_view bytes);
    bool handleCommandShortcut(const eacp::Graphics::KeyEvent& event);

    // Copy and paste where the platform actually puts them. macOS has Cmd+C/V
    // through handleCommandShortcut; everywhere else that modifier is the
    // Super/Windows key, which the desktop reserves, so it is Ctrl+Shift+C/V --
    // shifted because plain Ctrl+C is SIGINT and belongs to the shell.
    bool handleClipboardShortcut(const eacp::Graphics::KeyEvent& event);
    void performEditingShortcut(EditingShortcut shortcut);
    void clearScreenAndScrollback();
    bool handleSpecialKey(const eacp::Graphics::KeyEvent& event);
    bool handleKittyKey(const eacp::Graphics::KeyEvent& event);
    bool handleInterceptedKey(const eacp::Graphics::KeyEvent& event);
    bool handleKeyInCopyMode(const eacp::Graphics::KeyEvent& event);
    bool handleTypedKey(const eacp::Graphics::KeyEvent& event);
    int calculateScrollLinesForJump(ViewportJump jump) const;
    void copySelection();
    void applyGridSize();
    void scrollBy(int lines);

    bool handleCopyModeKey(const eacp::Graphics::KeyEvent& event);
    void applyCopyModeCommand(const CopyModeCommand& command);
    void cancelCopySelectionOrExit();
    void yankAndExitCopyMode();
    void beginCopySelection(bool wholeLines);
    void exitCopyMode();

    // Clamps to the buffer, scrolls the viewport to keep the cursor visible
    // and extends the live selection.
    void setCopyCursor(CellRef target);
    void updateCopySelection();
    std::string describeCopyModePosition() const;

    bool mouseReportingActive() const;
    bool isReportingMouseFor(const eacp::Graphics::MouseEvent& event) const;
    void sendMouseReport(const eacp::Graphics::MouseEvent& event,
                         int button,
                         bool pressed,
                         bool motion);
    int takeWholeWheelLines(float wheelDelta);
    void sendWheelAsArrowKeys(int lines);

    // The cell under a point, clamped to the grid.
    VisibleCell findCellUnderPointer(eacp::Graphics::Point pos) const;
    CellRef cellRefAt(eacp::Graphics::Point pos) const;
    void selectRow(CellRef cell);
    void selectWordAt(CellRef cell);
    void beginDragSelection(CellRef cell);
    bool hasSelection() const;

    long findTopVisibleAbsoluteRow() const;
    int calculateVisualRow(long absoluteRow) const;
    eacp::Graphics::Point calculateCellOrigin(int visualRow, int column) const;
    PaintedRow describeVisibleRow(int visualRow) const;
    GridPainter makePainter();
    std::vector<PaintedRow> describeVisibleRows() const;
    void rasterizeFrameGlyphs(GridPainter& painter,
                              const std::vector<PaintedRow>& rows,
                              const std::string& copyIndicator);
    void drawGrid(GridPainter& painter, const std::vector<PaintedRow>& rows);
    void drawCursorLayer(eacp::GPU::RenderPass& pass,
                         GridPainter& painter,
                         const std::string& copyIndicator);
    void drawCursor(GridPainter& painter);
    bool isCursorBlinkingNow() const;
    void drawCopyMode(GridPainter& painter, const std::string& indicator);

    Theme theme;
    std::string fontName;
    TermScreen screen;
    TermParser parser;
    std::string cwd;
    std::string title;
    float fontSize = 13.0f;
    std::optional<GlyphAtlas> atlas;
    std::optional<eacp::Sprites::SpriteRenderer> sprites;

    // Glyphs go through eacp-text's instanced renderer rather than the sprite
    // renderer: the mask atlas is R8Unorm, which a general sprite shader draws
    // as opaque red, and batching turns a screen of cells into one draw call.
    std::optional<eacp::Text::GlyphRenderer> glyphs;
    std::string paneShellId;
    bool commandTerminal = false;
    std::unique_ptr<Shell> shell;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    eacp::Threads::Timer blinkTimer;
    bool blinkOn = true;

    int scrollOffset = 0;
    float wheelRemainder = 0;

    bool selecting = false;
    CellRef selectionAnchor;
    SelectionRange selection;

    bool copyMode = false;
    bool copySelecting = false;
    bool copyLineSelect = false;
    CellRef copyAnchor;
    CellRef copyCursor;

    float marginX = 6.0f;
    float marginY = 4.0f;
    std::uint64_t renderedVersion = 0;
    std::uint64_t synchronizedOutputGeneration = 0;
    bool windowFocused = true;

    // Keys whose press went to the program under the kitty protocol, so only
    // their releases are reported (not a release of a leader-key press).
    std::vector<std::uint16_t> keysPressedForProgram;

    // Guards screen and parser, which the output thread writes. Recursive
    // because an entry point (setWindowFocused) may call another (refreshCursor).
    mutable std::recursive_mutex terminalLock;
    std::atomic<bool> parsedOutputQueued {false};

    // Last, so it stops (and joins its thread) before anything it parses into
    // is destroyed.
    std::unique_ptr<OutputParserThread> outputParser;
    FocusReports focusReports;
};
} // namespace term
