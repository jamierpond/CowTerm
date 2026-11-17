#include "TerminalView.h"

#include "CellText.h"

#include "Emulator/Debug.h"

#include <eacp/Core/App/Clipboard.h>

#include <algorithm>
#include <cmath>
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

    return config.makeCommandShell(shellId);
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

    createGlyphAtlasAtBackingScale();
    onBackingScaleChanged = [this](float) { rebuildGlyphAtlasForNewBackingScale(); };

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

    shell->onStatusChanged = [this, guard](const std::string& status)
    {
        if (!guard.expired())
            showShellStatus(status);
    };

    shell->onGridSetElsewhere = [this, guard](std::optional<PtySize> size)
    {
        if (!guard.expired())
            setFixedGridSize(size);
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

    parser.onCopyToClipboard = [this](const std::string& text)
    { runOnUiThread([text] { Clipboard::copyText(text); }); };

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
            followScrollbackMovement();
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
        scrollToLiveBottom();

    syncScrollPositionWithScrollback();
    reportScrolledBackStateIfChanged();
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

    runAfterDelayUnlessRestarted(synchronizedOutputTimeout,
                                 synchronizedOutputGeneration,
                                 [this]
                                 {
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
    scrollToLiveBottom();
    blinkOn = true;
    rowDrawings.invalidateRowsWithBlinkingText();
    shell->writeUserInput(bytes);
    repaintIfShown();
}

void TerminalView::sendText(std::string_view text)
{
    const auto lock = lockTerminal();
    auto bytes = std::string {text};
    std::replace(bytes.begin(), bytes.end(), '\n', '\r');
    sendAndScrollToBottom(bytes);
}

std::string TerminalView::captureVisibleText() const
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

        // The first layout is when the view knows its display; the scale it
        // was built at was a guess, and eacp reports no change from it.
        if (!glyphs)
        {
            glyphs.emplace();
            createGlyphAtlasAtBackingScale();
        }

        glyphs->setViewportSize({bounds.w, bounds.h});
    }

    if (hasGridSizeFromBounds)
        scheduleGridSizeOnceBoundsSettle();
    else
        applyGridSize();

    rowDrawings.invalidateAllRows();
    repaint();
}

// Ghostty's coalescing: while a window edge is dragged, the grid reflows and
// the shell gets SIGWINCH once, after the size has stopped changing.
void TerminalView::scheduleGridSizeOnceBoundsSettle()
{
    constexpr auto gridResizeQuietPeriod = Time::MS {25};

    runAfterDelayUnlessRestarted(gridResizeQuietPeriod,
                                 gridResizeGeneration,
                                 [this]
                                 {
                                     applyGridSize();
                                     rowDrawings.invalidateAllRows();
                                     repaintIfShown();
                                 });
}

// Each call bumps the generation, so only the latest scheduled action runs.
void TerminalView::runAfterDelayUnlessRestarted(Time::MS delay,
                                                std::uint64_t& generationCounter,
                                                std::function<void()> action)
{
    const auto generation = ++generationCounter;
    auto guard = std::weak_ptr<bool> {alive};

    Threads::callAfter(delay,
                       [this, guard, generation, &generationCounter, action = std::move(action)]
                       {
                           if (guard.expired() || generation != generationCounter)
                               return;

                           const auto lock = lockTerminal();
                           action();
                       });
}

void TerminalView::restoreTerminalState(const std::string& serialized)
{
    const auto state = parseTerminalState(serialized);

    if (!state)
        return;

    const auto lock = lockTerminal();
    terminal.restoreState(*state);
    scrollToLiveBottom();
    followScrollbackMovement();
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
    createGlyphAtlasAtBackingScale();
    rowDrawings.invalidateAllRows();
    applyGridSize();
    repaint();
}

// Glyphs are rasterized for the display's pixels: 3x on an iPhone, and
// whatever a Mac's screen is, so text is never scaled up or down.
void TerminalView::createGlyphAtlasAtBackingScale()
{
    atlas.emplace(fontName, fontSize, backingScale());
}

void TerminalView::rebuildGlyphAtlasForNewBackingScale()
{
    const auto lock = lockTerminal();
    createGlyphAtlasAtBackingScale();
    rowDrawings.invalidateAllRows();
    applyGridSize();
    repaintIfShown();
}

bool TerminalView::isPositiveSize(float width, float height)
{
    return width > 0 && height > 0;
}

// ---- rendering -------------------------------------------------------------

long TerminalView::findTopVisibleAbsoluteRow() const
{
    return scrollPosition.findTopAbsoluteRow(calculateScrollRange());
}

int TerminalView::calculateVisualRow(long absoluteRow) const
{
    return (int) (absoluteRow - findTopVisibleAbsoluteRow());
}

Graphics::Point TerminalView::calculateCellOrigin(int visualRow, int column) const
{
    const auto visualRowAsFloat = static_cast<float>(visualRow);
    const auto columnAsFloat = static_cast<float>(column);
    const auto rowTop = marginY + visualRowAsFloat * atlas->cellHeight();
    return {marginX + columnAsFloat * atlas->cellWidth(),
            rowTop - calculatePixelOffsetIntoTopRow()};
}

PaintedRow TerminalView::describeVisibleRow(int visualRow) const
{
    const auto absoluteRow = findTopVisibleAbsoluteRow() + visualRow;
    const auto rowTop = calculateCellOrigin(visualRow, 0).y;

    if (!isAbsoluteRowShown(absoluteRow))
        return {{}, absoluteRow, rowTop, 0};

    const auto& line = findLineAtAbsoluteRow(screen, absoluteRow);
    const auto lineLength = static_cast<int>(line.size());
    return {screen.resolveLine(line), absoluteRow, rowTop, std::min(lineLength, screen.columns())};
}

GridPainter TerminalView::makePainter()
{
    return {{*atlas, *sprites, *glyphs, theme, {marginX, marginY}}, selection, hoveredLink, blinkOn};
}

bool TerminalView::isCursorLayerVisible() const
{
    return copyMode || scrollPosition.isFollowingLiveBottom() || !shellStatus.empty();
}

std::string TerminalView::describeCopyModePosition() const
{
    if (!copyMode)
        return shellStatus;

    return "[" + std::to_string(calculateLinesFromBottom()) + "/"
           + std::to_string(screen.scrollbackSize()) + "]";
}

// Rows are cached by visual position, which only matches the screen's dirty
// rows at the bottom of the scrollback; scrolled back, every row is rebuilt
// and the next frame rebuilds again.
void TerminalView::updateRowDrawings(GridPainter& painter)
{
    const auto isScrolledBack = !scrollPosition.isFollowingLiveBottom();

    if (isScrolledBack)
        rowDrawings.invalidateAllRows();

    rowDrawings.updateRows({countRowsToDraw(),
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

    const long cursorAbsoluteRow = screen.scrollbackSize() + screen.cursor.y;
    const auto visualRow = calculateVisualRow(cursorAbsoluteRow);

    if (visualRow < 0 || visualRow >= countViewportRows())
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
                       screen.resolveLine(findLineAtAbsoluteRow(screen, cursorAbsoluteRow)),
                       screen.cursor.x);
}

void TerminalView::drawCopyMode(GridPainter& painter, const std::string& indicator)
{
    const auto visualRow = calculateVisualRow(copyCursor.row);

    if (visualRow >= 0 && visualRow < countViewportRows())
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
    {
        drawCopyMode(painter, copyIndicator);
    }
    else
    {
        drawCursor(painter);

        if (!shellStatus.empty())
            painter.drawCopyModeBadge(shellStatus, getLocalBounds().w);
    }

    sprites->flush();
    glyphs->flush(pass, atlas->atlas());
}

void TerminalView::render(GPU::Frame& frame)
{
    const auto lock = lockTerminal();
    auto pass = frame.beginPass({toColor(theme.background)});

    if (!sprites)
        return;

    syncScrollPositionWithScrollback();

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

    if (isCursorLayerVisible())
        drawCursorLayer(pass, painter, copyIndicator);

    if (scrollIndicatorFade.isVisible())
    {
        sprites->begin(pass);
        drawScrollIndicator(painter);
        sprites->flush();
    }

    if (edgeColour)
        drawEdge(pass, painter);

    renderedVersion = screen.version();
}

// ---- keyboard --------------------------------------------------------------

void TerminalView::clearScreenAndScrollback()
{
    screen.eraseInDisplay(3);
    screen.setCursor(0, 0);
    scrollToLiveBottom();
    followScrollbackMovement();
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
    const auto range = calculateScrollRange();
    const auto scrollableRows = range.liveBottomTopRow - range.oldestTopRow;
    const auto wholeRange = static_cast<int>(scrollableRows);
    const auto page = countViewportRows() - 1;

    switch (jump)
    {
        case ViewportJump::ToOldest:
            return wholeRange;

        case ViewportJump::ToNewest:
            return -wholeRange;

        case ViewportJump::PageTowardOldest:
            return page;

        case ViewportJump::PageTowardNewest:
            return -page;
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

// keyDown's handler table takes non-const members.
// NOLINTNEXTLINE(readability-make-member-function-const)
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

    pasteOnceSafe(text, [this, text] { pasteText(text); });
}

// A paste that could run commands goes to the host to ask first (Ghostty's
// paste protection, isPasteSafe); a safe one goes at once.
void TerminalView::pasteOnceSafe(const std::string& text, const eacp::Callback& send)
{
    const auto isSafe = [&]
    {
        const auto lock = lockTerminal();
        return isPasteSafe(text, screen.modes.bracketedPaste);
    }();

    if (isSafe)
    {
        send();
        return;
    }

    auto guard = std::weak_ptr<bool> {alive};
    confirmUnsafePaste(text,
                       [guard, send]
                       {
                           if (!guard.expired())
                               send();
                       });
}

void TerminalView::pasteText(const std::string& text)
{
    const auto lock = lockTerminal();
    sendAndScrollToBottom(preparePastedText(text, screen.modes.bracketedPaste));
}

void TerminalView::pasteThenPressEnter(const std::string& text)
{
    pasteOnceSafe(text,
                  [this, text]
                  {
                      constexpr auto enter = "\r";
                      const auto lock = lockTerminal();
                      sendAndScrollToBottom(
                          preparePastedText(text, screen.modes.bracketedPaste) + enter);
                  });
}

// ---- selection & mouse -----------------------------------------------------

VisibleCell TerminalView::findCellUnderPointer(Graphics::Point pos) const
{
    const auto column =
        std::clamp((int) ((pos.x - marginX) / atlas->cellWidth()), 0, screen.columns() - 1);
    const auto rowsFromTop = static_cast<int>((pos.y - marginY) / atlas->cellHeight());
    const auto visualRow = std::clamp(rowsFromTop, 0, countViewportRows() - 1);

    return {visualRow, column};
}

VisibleCell TerminalView::findGridCellUnderPointer(Graphics::Point pos) const
{
    const auto cell = findCellUnderPointer(pos);
    const auto rowsAboveGrid = countViewportRows() - screen.rows();
    const auto gridRow = std::clamp(cell.visualRow - rowsAboveGrid, 0, screen.rows() - 1);
    return {gridRow, cell.column};
}

CellRef TerminalView::cellRefAt(Graphics::Point pos) const
{
    const auto shiftedToRowTops =
        Graphics::Point {pos.x, pos.y + calculatePixelOffsetIntoTopRow()};
    const auto cell = findCellUnderPointer(shiftedToRowTops);
    const auto row = findTopVisibleAbsoluteRow() + cell.visualRow;
    return {std::max(row, findFirstShownAbsoluteRow()), cell.column};
}

std::string TerminalView::getSelectedText() const
{
    const auto lock = lockTerminal();

    if (!selection.active)
        return {};

    return extractTextBetween(screen, selection.start, selection.end);
}

void TerminalView::copySelection()
{
    if (selection.active)
        Clipboard::copyText(getSelectedText());
}

void TerminalView::selectRow(CellRef cell)
{
    const auto rows = findSoftWrappedLineRows(screen, cell.row);
    selection = {{rows.first, 0}, {rows.last, screen.columns() - 1}, true};
    rowDrawings.invalidateAllRows();
    selecting = false;
    repaintIfShown();
}

void TerminalView::selectWordAt(CellRef cell)
{
    selection = findWrappedWordAround(screen, cell);
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
    if (!scrollPosition.isFollowingLiveBottom())
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
    scrollToLiveBottom();
    repaintIfShown();
}

void TerminalView::setCopyCursor(CellRef target)
{
    placeCopyCursor(target);
    reportScrolledBackStateIfChanged();
    repaintIfShown();
}

void TerminalView::placeCopyCursor(CellRef target)
{
    copyCursor.row =
        std::clamp(target.row, findFirstAbsoluteRow(screen), findLastAbsoluteRow(screen));
    copyCursor.col = std::clamp(target.col, 0, screen.columns() - 1);

    syncScrollPositionWithScrollback();
    const auto visualRow = calculateVisualRow(copyCursor.row);
    const auto viewportRows = countViewportRows();

    if (visualRow < 0)
        scrollPosition.showTopRow(copyCursor.row, calculateScrollRange());
    else if (visualRow >= viewportRows)
        scrollPosition.showTopRow(copyCursor.row - viewportRows + 1, calculateScrollRange());

    updateCopySelection();
}

void TerminalView::updateCopySelection()
{
    if (!copySelecting)
        return;

    auto start = std::min(copyAnchor, copyCursor);
    auto end = std::max(copyAnchor, copyCursor);

    if (copyLineSelect)
    {
        start = {findSoftWrappedLineRows(screen, start.row).first, 0};
        end = {findSoftWrappedLineRows(screen, end.row).last, screen.columns() - 1};
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

    const auto cell = findGridCellUnderPointer(event.pos);
    send(encodeSgrMouseReport({button, pressed, motion, cell.column + 1, cell.visualRow + 1}));
}

std::optional<DetectedLink>
    TerminalView::findLinkUnderPointer(const MouseEvent& event) const
{
    const auto isLinkModifierHeld = Platform::isMac() ? event.modifiers.command
                                                      : event.modifiers.control;

    // A touch screen has no modifier to hold, so a tap opens the link.
    if (!isLinkModifierHeld && !event.fromTouch)
        return std::nullopt;

    return findLinkAt(screen, cellRefAt(event.pos));
}

bool TerminalView::openHyperlinkUnderPointer(const MouseEvent& event)
{
    if (event.button != Graphics::MouseButton::Left)
        return false;

    const auto link = findLinkUnderPointer(event);

    if (!link)
        return false;

    onOpenHyperlink(link->uri);
    return true;
}

eacp::Graphics::MouseCursor TerminalView::getCursorForHoveredLink() const
{
    if (hoveredLink)
        return eacp::Graphics::MouseCursor::PointingHand;

    return eacp::Graphics::MouseCursor::Default;
}

void TerminalView::setHoveredLink(std::optional<LinkSpan> link)
{
    if (!hoveredLink && !link)
        return;

    hoveredLink = link;
    setMouseCursor(getCursorForHoveredLink());
    rowDrawings.invalidateAllRows();
    repaintIfShown();
}

void TerminalView::mouseMoved(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    const auto link = findLinkUnderPointer(event);

    if (!link)
    {
        setHoveredLink(std::nullopt);
        return;
    }

    setHoveredLink(link->span);
}

void TerminalView::mouseExited(const MouseEvent&)
{
    const auto lock = lockTerminal();
    setHoveredLink(std::nullopt);
}

void TerminalView::mouseDown(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    onFocused();
    mousePressOwner = MousePressOwner::Nobody;

    if (isOnShellStatusNote(event.pos))
        return shell->takeSizeBack();

    if (openHyperlinkUnderPointer(event))
        return;

    if (isReportingMouseFor(event))
    {
        mousePressOwner = MousePressOwner::Program;
        sendMouseReport(event, (int) event.button, true, false);
        return;
    }

    if (event.button != Graphics::MouseButton::Left)
        return;

    mousePressOwner = MousePressOwner::Selection;
    const auto cell = cellRefAt(event.pos);

    if (event.clickCount >= 3)
        selectRow(cell);
    else if (event.clickCount == 2)
        selectWordAt(cell);
    else
        beginDragSelection(cell);
}

// A press the view saw decides; a drag or release with no press seen goes by
// the modes and Shift, as it did before.
bool TerminalView::isMouseGestureForProgram(const MouseEvent& event) const
{
    if (mousePressOwner == MousePressOwner::Nobody)
        return isReportingMouseFor(event);

    return mousePressOwner == MousePressOwner::Program;
}

void TerminalView::mouseDragged(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    if (isMouseGestureForProgram(event))
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
    const auto isForProgram = isMouseGestureForProgram(event);
    mousePressOwner = MousePressOwner::Nobody;

    if (isForProgram)
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
} // namespace term
