#include "TerminalView.h"

#include "CellText.h"

#include "CowTermCore/Debug.h"

#include <eacp/Core/App/Clipboard.h>

#include <algorithm>
#include <cstdio>
#include <random>

namespace term
{
using namespace eacp;
using Graphics::KeyEvent;
using Graphics::MouseEvent;

namespace
{
std::string generateShellId()
{
    auto device = std::random_device {};
    auto engine = std::mt19937_64 {device()};
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", (unsigned long long) engine());
    return buffer;
}

std::unique_ptr<Shell> shellFor(const TerminalConfig& config,
                                const std::string& shellId,
                                const std::string& command)
{
    if (command.empty())
        return config.makeShell(shellId);

    return std::make_unique<LocalShell>();
}

bool isPositiveSize(float width, float height)
{
    return width > 0 && height > 0;
}

std::vector<EnvironmentVariable> environmentFor(const TerminalConfig& config,
                                                const std::string& shellId,
                                                const std::string& command)
{
    if (!command.empty())
        return {};

    return config.environmentForShell(shellId);
}
} // namespace

TerminalView::TerminalView(const TerminalConfig& config,
                           const std::string& workingDirectory,
                           const std::string& shellIdToUse,
                           const std::string& commandToRun)
    : theme(config.theme)
    , fontName(config.font)
    , terminal(80, 24, theme)
    , fontSize(config.fontSize)
    , paneShellId(shellIdToUse.empty() ? generateShellId() : shellIdToUse)
    , commandTerminal(!commandToRun.empty())
    , shell(shellFor(config, paneShellId, commandToRun))
    , blinkTimer(
          [this]
          {
              const auto lock = lockTerminal();
              blinkOn = !blinkOn;
              rowDrawings.invalidateRowsWithBlinkingText();
              repaintUnlessSynchronized();
          },
          2)
{
    setSampleCount(1);
    setHandlesMouseEvents(true);
    setGrabsFocusOnMouseDown(true);

    atlas.emplace(fontName, fontSize);

    connectParserToUiThread();
    startOutputParserThread();

    startShell(config, workingDirectory, commandToRun);
}

void TerminalView::startShell(const TerminalConfig& config,
                              const std::string& workingDirectory,
                              const std::string& commandToRun)
{
    auto guard = std::weak_ptr<bool> {alive};

    shell->onRestoreState = [this, guard](const std::string& serialized)
    {
        if (!guard.expired())
            restoreTerminalState(serialized);
    };

    shell->start(
        {{screen.columns(), screen.rows()},
         workingDirectory,
         commandToRun,
         environmentFor(config, paneShellId, commandToRun)},
        [this](const std::string& data) { outputParser->append(data); },
        [this, guard]
        {
            Threads::callAsync(
                [this, guard]
                {
                    if (!guard.expired())
                        onShellExit();
                });
        });
}

// The shell stops delivering output before the thread that parses it goes,
// and that thread is gone before alive is reset under it.
TerminalView::~TerminalView()
{
    shell->detach();
    outputParser.reset();
    *alive = false;
    alive.reset();
}

void TerminalView::terminateShell()
{
    shell->terminate();
}

void TerminalView::connectParserToUiThread()
{
    // The parser runs on the output thread: replies go straight to the shell,
    // everything the host sees is handed to the UI thread.
    parser.respond = [this](std::string_view bytes) { send(bytes); };

    terminal.onTitleChanged = [this](const std::string& newTitle)
    {
        runOnUiThread(
            [this, newTitle]
            {
                title = newTitle;
                onTitleChanged(newTitle);
            });
    };

    terminal.onCwdChanged = [this](const std::string& newCwd)
    {
        runOnUiThread(
            [this, newCwd]
            {
                cwd = newCwd;
                onCwdChanged(newCwd);
            });
    };

    parser.onNotify = [this](const std::string& text)
    { runOnUiThread([this, text] { onNotify(text); }); };

    parser.onSynchronizedOutputStarted = [this]
    { runOnUiThread([this] { startSynchronizedOutputTimeout(); }); };
}

void TerminalView::startOutputParserThread()
{
    outputParser = std::make_unique<OutputParserThread>(
        [this](std::string_view bytes)
        {
            const auto lock = lockTerminal();
            terminal.feed(bytes);
        },
        [this] { requestShowParsedOutput(); });
}

std::unique_lock<std::recursive_mutex> TerminalView::lockTerminal() const
{
    return std::unique_lock {terminalLock};
}

void TerminalView::runOnUiThread(std::function<void()> action)
{
    auto guard = std::weak_ptr<bool> {alive};

    Threads::callAsync(
        [guard, action = std::move(action)]
        {
            if (!guard.expired())
                action();
        });
}

void TerminalView::waitForParsedOutput()
{
    outputParser->waitUntilIdle();
}

void TerminalView::refreshCursor()
{
    const auto lock = lockTerminal();
    blinkOn = true;
    rowDrawings.invalidateRowsWithBlinkingText();
    reportFocusIfChanged();
    repaintIfShown();
}

void TerminalView::setWindowFocused(bool focused)
{
    windowFocused = focused;
    refreshCursor();
}

void TerminalView::reportFocusIfChanged()
{
    const auto report = focusReports.update(windowFocused && hasFocus(), screen.modes.focusEvents);

    if (!report.empty())
        send(report);
}

// Called on the output thread after each batch; at most one showParsedOutput
// is queued however fast batches come.
void TerminalView::requestShowParsedOutput()
{
    if (!parsedOutputQueued.exchange(true))
        runOnUiThread([this] { showParsedOutput(); });
}

void TerminalView::showParsedOutput()
{
    parsedOutputQueued = false;
    const auto lock = lockTerminal();

    if (screen.modes.altScreen)
        scrollOffset = 0;

    scrollOffset = std::min(scrollOffset, screen.scrollbackSize());
    repaintUnlessSynchronized();
}

void TerminalView::repaintUnlessSynchronized()
{
    if (!screen.modes.synchronizedOutput)
        repaintIfShown();
}

bool TerminalView::isShownOnScreen() const
{
    const auto bounds = getLocalBounds();
    return getWindow() != nullptr && isVisible() && isPositiveSize(bounds.w, bounds.h);
}

// A pane in a background tab or behind a zoomed split skips its frames; whoever
// shows it again repaints it: resized(), or SessionView::repaintAllPanes.
void TerminalView::repaintIfShown()
{
    if (isShownOnScreen())
        repaint();
}

// Ghostty's timeout: a program that set mode 2026 and never reset it (it
// crashed mid-frame) gets its screen shown a second after its last set.
void TerminalView::startSynchronizedOutputTimeout()
{
    constexpr auto synchronizedOutputTimeout = Time::MS {1000};
    const auto generation = ++synchronizedOutputGeneration;
    auto guard = std::weak_ptr<bool> {alive};

    Threads::callAfter(synchronizedOutputTimeout,
                       [this, guard, generation]
                       {
                           if (guard.expired() || generation != synchronizedOutputGeneration)
                               return;

                           const auto lock = lockTerminal();
                           screen.modes.synchronizedOutput = false;
                           repaintIfShown();
                       });
}

void TerminalView::send(std::string_view bytes)
{
    shell->write(bytes);
}

void TerminalView::sendAndScrollToBottom(std::string_view bytes)
{
    scrollOffset = 0;
    blinkOn = true;
    rowDrawings.invalidateRowsWithBlinkingText();
    send(bytes);
    repaintIfShown();
}

void TerminalView::sendText(std::string_view text)
{
    const auto lock = lockTerminal();
    auto bytes = std::string {text};
    std::replace(bytes.begin(), bytes.end(), '\n', '\r');
    sendAndScrollToBottom(bytes);
}

std::string TerminalView::debugScreenText() const
{
    const auto lock = lockTerminal();
    auto out = std::string {};

    for (int row = 0; row < screen.rows(); ++row)
    {
        out += extractVisibleRowText(screen, screen.lineAt(row, 0), screen.columns());
        out += '\n';
    }

    return out;
}

void TerminalView::resized()
{
    const auto lock = lockTerminal();
    GPUView::resized();

    const auto bounds = getLocalBounds();

    if (isPositiveSize(bounds.w, bounds.h))
    {
        sprites.emplace(Graphics::Point {bounds.w, bounds.h}, sampleCount());

        if (!glyphs)
            glyphs.emplace();

        glyphs->setViewportSize({bounds.w, bounds.h});
    }

    applyGridSize();
    rowDrawings.invalidateAllRows();
    repaint();
}

void TerminalView::applyGridSize()
{
    const auto bounds = getLocalBounds();
    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();

    if (!isPositiveSize(cellW, cellH) || !isPositiveSize(bounds.w, bounds.h))
        return;

    const auto cols = std::max(2, (int) ((bounds.w - 2 * marginX) / cellW));
    const auto rows = std::max(1, (int) ((bounds.h - 2 * marginY) / cellH));

    if (commandTerminal)
        debugLog("[grid] popup bounds w=%.1f h=%.1f cell=%.2fx%.2f font=%.1f -> %dx%d\n",
                 bounds.w, bounds.h, cellW, cellH, fontSize, cols, rows);

    if (cols == screen.columns() && rows == screen.rows())
        return;

    screen.resize(cols, rows);
    shell->resize({cols, rows});
    scrollOffset = std::min(scrollOffset, screen.scrollbackSize());
}

void TerminalView::restoreTerminalState(const std::string& serialized)
{
    const auto state = parseTerminalState(serialized);

    if (!state)
        return;

    const auto lock = lockTerminal();
    terminal.restoreState(*state);
    scrollOffset = 0;
    title = terminal.getTitle();
    cwd = terminal.getWorkingDirectory();

    if (screen.modes.synchronizedOutput)
        startSynchronizedOutputTimeout();

    onTitleChanged(title);
    onCwdChanged(cwd);
    applyGridSize();
    repaint();
}

void TerminalView::setFontSize(float newSize)
{
    const auto lock = lockTerminal();
    const auto clamped = std::clamp(newSize, minFontSize, maxFontSize);

    if (clamped == fontSize)
        return;

    fontSize = clamped;
    atlas.emplace(fontName, fontSize);
    rowDrawings.invalidateAllRows();
    applyGridSize();
    repaint();
}

void TerminalView::scrollBy(int lines)
{
    const auto previous = scrollOffset;
    scrollOffset = std::clamp(scrollOffset + lines, 0, screen.scrollbackSize());

    if (scrollOffset != previous)
        repaintIfShown();
}

// ---- rendering -------------------------------------------------------------

long TerminalView::findTopVisibleAbsoluteRow() const
{
    return (long) screen.scrollbackSize() - scrollOffset;
}

int TerminalView::calculateVisualRow(long absoluteRow) const
{
    return (int) (absoluteRow - findTopVisibleAbsoluteRow());
}

Graphics::Point TerminalView::calculateCellOrigin(int visualRow, int column) const
{
    return {marginX + (float) column * atlas->cellWidth(),
            marginY + (float) visualRow * atlas->cellHeight()};
}

PaintedRow TerminalView::describeVisibleRow(int visualRow) const
{
    const auto& line = screen.lineAt(visualRow, scrollOffset);

    return {screen.resolveLine(line),
            findTopVisibleAbsoluteRow() + visualRow,
            marginY + (float) visualRow * atlas->cellHeight(),
            std::min((int) line.size(), screen.columns())};
}

GridPainter TerminalView::makePainter()
{
    return {{*atlas, *sprites, *glyphs, theme, {marginX, marginY}}, selection, blinkOn};
}

std::string TerminalView::describeCopyModePosition() const
{
    if (!copyMode)
        return {};

    return "[" + std::to_string(scrollOffset) + "/"
           + std::to_string(screen.scrollbackSize()) + "]";
}

// Rows are cached by visual position, which only matches the screen's dirty
// rows at the bottom of the scrollback; scrolled back, every row is rebuilt
// and the next frame rebuilds again.
void TerminalView::updateRowDrawings(GridPainter& painter)
{
    const auto isScrolledBack = scrollOffset != 0;

    if (isScrolledBack)
        rowDrawings.invalidateAllRows();

    rowDrawings.updateRows({screen.rows(),
                            [this](int row) { return screen.isRowDirty(row); },
                            [this, &painter](int row)
                            { return painter.buildRowDrawing(describeVisibleRow(row)); },
                            [this] { return atlas->generation(); }});
    screen.clearDirtyRows();

    if (isScrolledBack)
        rowDrawings.invalidateAllRows();
}

bool TerminalView::isCursorBlinkingNow() const
{
    return screen.modes.cursorBlink && isBlinkingCursorShape(parser.cursorShape());
}

void TerminalView::drawCursor(GridPainter& painter)
{
    if (!screen.modes.showCursor)
        return;

    const auto visualRow = screen.cursor.y + scrollOffset;

    if (visualRow >= screen.rows())
        return;

    const auto origin = calculateCellOrigin(visualRow, screen.cursor.x);

    if (!hasFocus())
    {
        painter.drawHollowCursor(origin);
        return;
    }

    if (isCursorBlinkingNow() && !blinkOn)
        return;

    painter.drawCursor(origin,
                       findCursorStyle(parser.cursorShape()),
                       screen.resolveLine(screen.lineAt(visualRow, scrollOffset)),
                       screen.cursor.x);
}

void TerminalView::drawCopyMode(GridPainter& painter, const std::string& indicator)
{
    const auto visualRow = calculateVisualRow(copyCursor.row);

    if (visualRow >= 0 && visualRow < screen.rows())
        painter.drawBlockCursor(calculateCellOrigin(visualRow, copyCursor.col),
                                screen.resolveLine(findLineAtAbsoluteRow(screen, copyCursor.row)),
                                copyCursor.col);

    painter.drawCopyModeBadge(indicator, getLocalBounds().w);
}

// The cursor draws a filled block through the sprite renderer and then the
// glyph on top of it, so both pipelines have to be rebound: the grid's glyph
// flush left its own pipeline in place.
void TerminalView::drawCursorLayer(GPU::RenderPass& pass,
                                   GridPainter& painter,
                                   const std::string& copyIndicator)
{
    sprites->begin(pass);
    glyphs->begin();

    if (copyMode)
        drawCopyMode(painter, copyIndicator);
    else
        drawCursor(painter);

    sprites->flush();
    glyphs->flush(pass, atlas->atlas());
}

void TerminalView::render(GPU::Frame& frame)
{
    const auto lock = lockTerminal();
    auto pass = frame.beginPass({toColor(theme.background)});

    if (!sprites)
        return;

    sprites->begin(pass);
    glyphs->begin();

    auto painter = makePainter();
    const auto copyIndicator = describeCopyModePosition();

    painter.rasterizeTextGlyphs(copyIndicator);
    updateRowDrawings(painter);
    atlas->commit();
    painter.submitRowDrawings(rowDrawings.getRows());

    // The backgrounds have to reach the pass before the glyphs do. A sprite
    // batch is drawn when the pass ends -- after every glyph -- so left queued
    // it would land on top of the text rather than behind it, and a second
    // begin() before then discards it outright.
    sprites->flush();

    // Every cell's glyph is queued by now, so the whole screen submits as one
    // instanced draw rather than one call per character.
    glyphs->flush(pass, atlas->atlas());

    if (copyMode || scrollOffset == 0)
        drawCursorLayer(pass, painter, copyIndicator);

    renderedVersion = screen.version();
}

// ---- keyboard --------------------------------------------------------------

void TerminalView::clearScreenAndScrollback()
{
    screen.eraseInDisplay(3);
    screen.setCursor(0, 0);
    scrollOffset = 0;
    send("\014");
    repaintIfShown();
}

void TerminalView::performEditingShortcut(EditingShortcut shortcut)
{
    switch (shortcut)
    {
        case EditingShortcut::Copy:
            copySelection();
            return;

        case EditingShortcut::Paste:
            paste();
            return;

        case EditingShortcut::ClearScreen:
            clearScreenAndScrollback();
            return;

        case EditingShortcut::Swallow:
            return;
    }
}

bool TerminalView::handleCommandShortcut(const KeyEvent& event)
{
    const auto shortcut = findCommandShortcut(event);

    if (!shortcut)
        return false;

    performEditingShortcut(*shortcut);
    return true;
}

bool TerminalView::handleClipboardShortcut(const KeyEvent& event)
{
    // macOS already has these on Command, and Ctrl+Shift+C there is the shell's.
    if constexpr (eacp::Platform::isMac())
        return false;

    const auto shortcut = findControlShiftClipboardShortcut(event);

    if (!shortcut)
        return false;

    performEditingShortcut(*shortcut);
    return true;
}

int TerminalView::calculateScrollLinesForJump(ViewportJump jump) const
{
    switch (jump)
    {
        case ViewportJump::ToOldest:
            return screen.scrollbackSize();

        case ViewportJump::ToNewest:
            return -screen.scrollbackSize();

        case ViewportJump::PageTowardOldest:
            return screen.rows() - 1;

        case ViewportJump::PageTowardNewest:
            return -(screen.rows() - 1);
    }

    return 0;
}

bool TerminalView::handleSpecialKey(const KeyEvent& event)
{
    const auto response = encodeSpecialKey(event, screen.modes.appCursorKeys);

    if (!response)
        return false;

    if (const auto* jump = std::get_if<ViewportJump>(&*response))
        scrollBy(calculateScrollLinesForJump(*jump));
    else
        sendAndScrollToBottom(std::get<std::string>(*response));

    return true;
}

bool TerminalView::handleInterceptedKey(const KeyEvent& event)
{
    return interceptKey(event);
}

bool TerminalView::handleKeyInCopyMode(const KeyEvent& event)
{
    return copyMode && handleCopyModeKey(event);
}

bool TerminalView::handleTypedKey(const KeyEvent& event)
{
    const auto bytes = encodeTypedKey(event);

    if (!bytes)
        return false;

    sendAndScrollToBottom(*bytes);
    return true;
}

void TerminalView::keyDown(const KeyEvent& event)
{
    const auto lock = lockTerminal();
    using KeyHandler = bool (TerminalView::*)(const KeyEvent&);
    static constexpr auto keyHandlersInOrder =
        std::array<KeyHandler, 7> {&TerminalView::handleInterceptedKey,
                                   &TerminalView::handleKeyInCopyMode,
                                   &TerminalView::handleClipboardShortcut,
                                   &TerminalView::handleCommandShortcut,
                                   &TerminalView::handleKittyKey,
                                   &TerminalView::handleSpecialKey,
                                   &TerminalView::handleTypedKey};

    for (const auto handler: keyHandlersInOrder)
    {
        if ((this->*handler)(event))
            return;
    }
}

bool TerminalView::handleKittyKey(const KeyEvent& event)
{
    const auto flags = screen.kittyKeyboard().current();

    if (flags == 0)
        return false;

    if (const auto jump = findViewportJump(event))
    {
        scrollBy(calculateScrollLinesForJump(*jump));
        return true;
    }

    const auto action = event.isRepeat ? KeyAction::Repeat : KeyAction::Press;
    const auto bytes = encodeKittyKey(event, action, flags);

    if (!bytes.empty())
        sendAndScrollToBottom(bytes);

    const auto alreadyHeld = std::find(keysPressedForProgram.begin(),
                                       keysPressedForProgram.end(),
                                       event.keyCode)
                             != keysPressedForProgram.end();

    if (!alreadyHeld)
        keysPressedForProgram.push_back(event.keyCode);

    return true;
}

void TerminalView::keyUp(const KeyEvent& event)
{
    const auto lock = lockTerminal();
    const auto pressed =
        std::find(keysPressedForProgram.begin(), keysPressedForProgram.end(), event.keyCode);

    if (pressed == keysPressedForProgram.end())
        return;

    keysPressedForProgram.erase(pressed);
    const auto bytes =
        encodeKittyKey(event, KeyAction::Release, screen.kittyKeyboard().current());

    if (!bytes.empty())
        send(bytes);
}

void TerminalView::paste()
{
    const auto text = Clipboard::getText();

    if (text.empty())
        return;

    const auto isSafe = [&]
    {
        const auto lock = lockTerminal();
        return isPasteSafe(text, screen.modes.bracketedPaste);
    }();

    if (isSafe)
    {
        pasteText(text);
        return;
    }

    auto guard = std::weak_ptr<bool> {alive};
    confirmUnsafePaste(text,
                       [this, guard, text]
                       {
                           if (!guard.expired())
                               pasteText(text);
                       });
}

void TerminalView::pasteText(const std::string& text)
{
    const auto lock = lockTerminal();
    sendAndScrollToBottom(preparePastedText(text, screen.modes.bracketedPaste));
}

// ---- selection & mouse -----------------------------------------------------

VisibleCell TerminalView::findCellUnderPointer(Graphics::Point pos) const
{
    const auto column =
        std::clamp((int) ((pos.x - marginX) / atlas->cellWidth()), 0, screen.columns() - 1);
    const auto visualRow =
        std::clamp((int) ((pos.y - marginY) / atlas->cellHeight()), 0, screen.rows() - 1);

    return {visualRow, column};
}

CellRef TerminalView::cellRefAt(Graphics::Point pos) const
{
    const auto cell = findCellUnderPointer(pos);
    return {findTopVisibleAbsoluteRow() + cell.visualRow, cell.column};
}

bool TerminalView::hasSelection() const
{
    return selection.active && !(selection.start == selection.end && !selecting);
}

void TerminalView::copySelection()
{
    if (hasSelection())
        Clipboard::copyText(extractTextBetween(screen, selection.start, selection.end));
}

void TerminalView::selectRow(CellRef cell)
{
    selection = {{cell.row, 0}, {cell.row, screen.columns() - 1}, true};
    rowDrawings.invalidateAllRows();
    selecting = false;
    repaintIfShown();
}

void TerminalView::selectWordAt(CellRef cell)
{
    const auto word = findWordAround(findLineAtAbsoluteRow(screen, cell.row), cell.col);

    selection = {{cell.row, word.firstColumn}, {cell.row, word.lastColumn}, word.isOnWord};
    rowDrawings.invalidateAllRows();
    selecting = false;
    repaintIfShown();
}

void TerminalView::beginDragSelection(CellRef cell)
{
    selectionAnchor = cell;
    selection = {cell, cell, false};
    rowDrawings.invalidateAllRows();
    selecting = true;
    repaintIfShown();
}

// ---- copy mode -------------------------------------------------------------

void TerminalView::enterCopyMode()
{
    const auto lock = lockTerminal();
    if (copyMode)
        return;

    copyMode = true;
    copySelecting = false;
    copyLineSelect = false;

    // Already scrolled up: keep the view and start at its top-left, like
    // tmux. Otherwise start at the terminal cursor.
    if (scrollOffset > 0)
        setCopyCursor({findTopVisibleAbsoluteRow(), 0});
    else
        setCopyCursor(
            {(long) screen.scrollbackSize() + screen.cursor.y, screen.cursor.x});
}

void TerminalView::exitCopyMode()
{
    copyMode = false;
    copySelecting = false;
    copyLineSelect = false;
    selection.active = false;
    rowDrawings.invalidateAllRows();
    scrollOffset = 0;
    repaintIfShown();
}

void TerminalView::setCopyCursor(CellRef target)
{
    copyCursor.row = std::clamp(target.row, 0L, findLastAbsoluteRow(screen));
    copyCursor.col = std::clamp(target.col, 0, screen.columns() - 1);

    const auto visualRow = calculateVisualRow(copyCursor.row);

    if (visualRow < 0)
        scrollOffset = (int) ((long) screen.scrollbackSize() - copyCursor.row);
    else if (visualRow >= screen.rows())
        scrollOffset = (int) ((long) screen.scrollbackSize() - copyCursor.row
                              + screen.rows() - 1);

    scrollOffset = std::clamp(scrollOffset, 0, screen.scrollbackSize());
    updateCopySelection();
    repaintIfShown();
}

void TerminalView::updateCopySelection()
{
    if (!copySelecting)
        return;

    auto start = std::min(copyAnchor, copyCursor);
    auto end = std::max(copyAnchor, copyCursor);

    if (copyLineSelect)
    {
        start.col = 0;
        end.col = screen.columns() - 1;
    }

    selection = {start, end, true};
    rowDrawings.invalidateAllRows();
}

void TerminalView::cancelCopySelectionOrExit()
{
    if (!copySelecting)
    {
        exitCopyMode();
        return;
    }

    copySelecting = false;
    copyLineSelect = false;
    selection.active = false;
    rowDrawings.invalidateAllRows();
    repaintIfShown();
}

void TerminalView::yankAndExitCopyMode()
{
    if (selection.active)
        Clipboard::copyText(extractTextBetween(screen, selection.start, selection.end));

    exitCopyMode();
}

void TerminalView::beginCopySelection(bool wholeLines)
{
    copySelecting = true;
    copyLineSelect = wholeLines;
    copyAnchor = copyCursor;
    updateCopySelection();
    repaintIfShown();
}

void TerminalView::applyCopyModeCommand(const CopyModeCommand& command)
{
    switch (command.action)
    {
        case CopyModeAction::Move:
            setCopyCursor(findCopyModeMotionTarget(command.motion, copyCursor, screen));
            return;

        case CopyModeAction::CancelSelectionOrExit:
            cancelCopySelectionOrExit();
            return;

        case CopyModeAction::Exit:
            exitCopyMode();
            return;

        case CopyModeAction::YankAndExit:
            yankAndExitCopyMode();
            return;

        case CopyModeAction::BeginCharacterSelection:
            beginCopySelection(false);
            return;

        case CopyModeAction::BeginLineSelection:
            beginCopySelection(true);
            return;

        case CopyModeAction::Nothing:
            return;
    }
}

bool TerminalView::handleCopyModeKey(const KeyEvent& event)
{
    const auto command = findCopyModeCommand(event);

    if (!command)
        return false;

    applyCopyModeCommand(*command);
    return true;
}

// ---- mouse -----------------------------------------------------------------

bool TerminalView::mouseReportingActive() const
{
    const auto& modes = screen.modes;
    return modes.mouseButtons || modes.mouseDrag || modes.mouseMotion;
}

bool TerminalView::isReportingMouseFor(const MouseEvent& event) const
{
    return mouseReportingActive() && !event.modifiers.shift;
}

void TerminalView::sendMouseReport(const MouseEvent& event,
                                   int button,
                                   bool pressed,
                                   bool motion)
{
    if (!screen.modes.mouseSgr)
        return;

    const auto cell = findCellUnderPointer(event.pos);
    send(encodeSgrMouseReport({button, pressed, motion, cell.column + 1, cell.visualRow + 1}));
}

bool TerminalView::openHyperlinkUnderPointer(const MouseEvent& event)
{
    const auto isLinkClick = Platform::isMac() ? event.modifiers.command
                                               : event.modifiers.control;

    if (!isLinkClick || event.button != Graphics::MouseButton::Left)
        return false;

    const auto cell = findCellUnderPointer(event.pos);
    const auto& line = screen.lineAt(cell.visualRow, scrollOffset);
    const auto uri = screen.findHyperlinkUri(line[(std::size_t) cell.column].hyperlink);

    if (!hasOpenableScheme(uri))
        return false;

    onOpenHyperlink(std::string {uri});
    return true;
}

void TerminalView::mouseDown(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    onFocused();

    if (openHyperlinkUnderPointer(event))
        return;

    if (isReportingMouseFor(event))
    {
        sendMouseReport(event, (int) event.button, true, false);
        return;
    }

    if (event.button != Graphics::MouseButton::Left)
        return;

    const auto cell = cellRefAt(event.pos);

    if (event.clickCount >= 3)
        selectRow(cell);
    else if (event.clickCount == 2)
        selectWordAt(cell);
    else
        beginDragSelection(cell);
}

void TerminalView::mouseDragged(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    if (isReportingMouseFor(event))
    {
        if (screen.modes.mouseDrag || screen.modes.mouseMotion)
            sendMouseReport(event, (int) event.button, true, true);

        return;
    }

    if (!selecting)
        return;

    const auto cell = cellRefAt(event.pos);
    selection = {std::min(selectionAnchor, cell), std::max(selectionAnchor, cell), true};
    rowDrawings.invalidateAllRows();
    repaintIfShown();
}

void TerminalView::mouseUp(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    if (isReportingMouseFor(event))
    {
        sendMouseReport(event, (int) event.button, false, false);
        return;
    }

    if (selecting && selection.start == selection.end)
    {
        selection.active = false;
        rowDrawings.invalidateAllRows();
    }

    selecting = false;
    repaintIfShown();
}

int TerminalView::takeWholeWheelLines(float wheelDelta)
{
    wheelRemainder += wheelDelta;
    const auto lines = (int) wheelRemainder;
    wheelRemainder -= (float) lines;
    return lines;
}

// Full-screen apps get wheel motion as arrow keys, the classic fallback when no
// mouse mode is active.
void TerminalView::sendWheelAsArrowKeys(int lines)
{
    const auto arrows = encodeWheelAsArrowKeys(lines, screen.modes.appCursorKeys);

    for (auto i = 0; i < arrows.count; ++i)
        send(arrows.key);
}

void TerminalView::mouseWheel(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    if (isReportingMouseFor(event))
    {
        const auto button = event.delta.y > 0 ? wheelUpMouseButton : wheelDownMouseButton;
        sendMouseReport(event, button, true, false);
        return;
    }

    const auto lines = takeWholeWheelLines(event.delta.y);

    if (lines == 0)
        return;

    if (screen.modes.altScreen)
        sendWheelAsArrowKeys(lines);
    else
        scrollBy(lines);
}
} // namespace term
