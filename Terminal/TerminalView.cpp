#include "TerminalView.h"

#include <eacp/Core/App/Clipboard.h>

#include <algorithm>
#include <cstdio>
#include <random>

namespace term
{
using namespace eacp;
namespace KeyCode = Graphics::KeyCode;
using Graphics::KeyEvent;
using Graphics::MouseEvent;

namespace
{
namespace MacKey
{
constexpr std::uint16_t Home = 0x73;
constexpr std::uint16_t End = 0x77;
constexpr std::uint16_t PageUp = 0x74;
constexpr std::uint16_t PageDown = 0x79;
constexpr std::uint16_t ForwardDelete = 0x75;
constexpr std::uint16_t KeypadEnter = 0x4c;
} // namespace MacKey

int modifierCode(const Graphics::ModifierKeys& mods)
{
    return 1 + (mods.shift ? 1 : 0) + (mods.alt ? 2 : 0) + (mods.control ? 4 : 0);
}

std::string csiWithModifiers(char final, const Graphics::ModifierKeys& mods)
{
    const auto code = modifierCode(mods);

    if (code == 1)
        return std::string {"\033["} + final;

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "\033[1;%d%c", code, final);
    return buffer;
}

std::string tildeKey(int number, const Graphics::ModifierKeys& mods)
{
    char buffer[16];
    const auto code = modifierCode(mods);

    if (code == 1)
        std::snprintf(buffer, sizeof(buffer), "\033[%d~", number);
    else
        std::snprintf(buffer, sizeof(buffer), "\033[%d;%d~", number, code);

    return buffer;
}

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80)
    {
        out.push_back((char) cp);
    }
    else if (cp < 0x800)
    {
        out.push_back((char) (0xc0 | (cp >> 6)));
        out.push_back((char) (0x80 | (cp & 0x3f)));
    }
    else if (cp < 0x10000)
    {
        out.push_back((char) (0xe0 | (cp >> 12)));
        out.push_back((char) (0x80 | ((cp >> 6) & 0x3f)));
        out.push_back((char) (0x80 | (cp & 0x3f)));
    }
    else
    {
        out.push_back((char) (0xf0 | (cp >> 18)));
        out.push_back((char) (0x80 | ((cp >> 12) & 0x3f)));
        out.push_back((char) (0x80 | ((cp >> 6) & 0x3f)));
        out.push_back((char) (0x80 | (cp & 0x3f)));
    }
}
} // namespace

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

std::unique_ptr<Shell> shellFor(const std::string& shellId,
                                const std::string& command,
                                std::unique_ptr<Shell> shellOverride)
{
    if (shellOverride != nullptr)
        return shellOverride;

    if (command.empty())
        return makeShell(shellId);

    return std::make_unique<LocalShell>();
}
} // namespace

TerminalView::TerminalView(const AppConfig& config,
                           const std::string& workingDirectory,
                           const std::string& shellIdToUse,
                           const std::string& commandToRun,
                           std::unique_ptr<Shell> shellOverride)
    : theme(themeByName(config.theme))
    , fontName(config.font)
    , screen(80, 24, theme)
    , parser(screen, theme)
    , fontSize(config.fontSize)
    , paneShellId(shellIdToUse.empty() ? generateShellId() : shellIdToUse)
    , shell(shellFor(paneShellId, commandToRun, std::move(shellOverride)))
    , blinkTimer(
          [this]
          {
              blinkOn = !blinkOn;
              repaint();
          },
          2)
{
    setSampleCount(1);
    setHandlesMouseEvents(true);
    setGrabsFocusOnMouseDown(true);

    atlas.emplace(fontName, fontSize);

    parser.respond = [this](std::string_view bytes) { send(bytes); };

    parser.onTitleChanged = [this](const std::string& newTitle)
    {
        title = newTitle;
        onTitleChanged(newTitle);
    };

    parser.onCwdChanged = [this](const std::string& newCwd)
    {
        cwd = newCwd;
        onCwdChanged(newCwd);
    };

    parser.onNotify = [this](const std::string& text) { onNotify(text); };

    auto guard = std::weak_ptr<bool> {alive};

    shell->start(
        {{screen.columns(), screen.rows()}, workingDirectory, commandToRun},
        [this, guard](const std::string& data)
        {
            {
                auto lock = std::scoped_lock {outputLock};
                pendingOutput += data;
            }

            Threads::callAsync(
                [this, guard]
                {
                    if (!guard.expired())
                        flushOutput();
                });
        },
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

TerminalView::~TerminalView()
{
    *alive = false;
    alive.reset();
    shell->detach();
}

void TerminalView::terminateShell()
{
    shell->terminate();
}

void TerminalView::refreshCursor()
{
    blinkOn = true;
    repaint();
}

void TerminalView::flushOutput()
{
    auto data = std::string {};

    {
        auto lock = std::scoped_lock {outputLock};
        data.swap(pendingOutput);
    }

    if (data.empty())
        return;

    // A remote pane's grid follows the remote GUI, announced just before
    // its snapshot arrives — resize ahead of parsing so the snapshot lands
    // on the dimensions it was serialized for.
    if (const auto fixed = shell->fixedSize();
        fixed && (fixed->cols != screen.columns() || fixed->rows != screen.rows()))
    {
        screen.resize(fixed->cols, fixed->rows);
    }

    parser.feed(data);
    onOutput(data);

    if (screen.modes.altScreen)
        scrollOffset = 0;

    scrollOffset = std::min(scrollOffset, screen.scrollbackSize());
    repaint();
}

void TerminalView::send(std::string_view bytes)
{
    shell->write(bytes);
}

void TerminalView::sendAndScrollToBottom(std::string_view bytes)
{
    scrollOffset = 0;
    blinkOn = true;
    send(bytes);
    repaint();
}

void TerminalView::sendText(std::string_view text)
{
    auto bytes = std::string {text};
    std::replace(bytes.begin(), bytes.end(), '\n', '\r');
    sendAndScrollToBottom(bytes);
}

void TerminalView::resized()
{
    GPUView::resized();

    const auto bounds = getLocalBounds();

    if (bounds.w > 0 && bounds.h > 0)
    {
        sprites.emplace(Graphics::Point {bounds.w, bounds.h}, sampleCount());

        if (!glyphs)
            glyphs.emplace();

        glyphs->setViewportSize({bounds.w, bounds.h});
    }

    applyGridSize();
    repaint();
}

void TerminalView::applyGridSize()
{
    // A fixed-size shell (remote pane) keeps its own dimensions no matter
    // what this view's bounds say; overflow clips, a smaller grid floats.
    if (const auto fixed = shell->fixedSize())
    {
        if (fixed->cols != screen.columns() || fixed->rows != screen.rows())
        {
            screen.resize(fixed->cols, fixed->rows);
            scrollOffset = std::min(scrollOffset, screen.scrollbackSize());
        }

        return;
    }

    const auto bounds = getLocalBounds();
    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();

    if (cellW <= 0 || cellH <= 0 || bounds.w <= 0 || bounds.h <= 0)
        return;

    const auto cols = std::max(2, (int) ((bounds.w - 2 * marginX) / cellW));
    const auto rows = std::max(1, (int) ((bounds.h - 2 * marginY) / cellH));

    if (cols == screen.columns() && rows == screen.rows())
        return;

    screen.resize(cols, rows);
    shell->resize({cols, rows});
    scrollOffset = std::min(scrollOffset, screen.scrollbackSize());
}

void TerminalView::setFontSize(float newSize)
{
    const auto clamped = std::clamp(newSize, minFontSize, maxFontSize);

    if (clamped == fontSize)
        return;

    fontSize = clamped;
    atlas.emplace(fontName, fontSize);
    applyGridSize();
    repaint();
}

void TerminalView::scrollBy(int lines)
{
    const auto previous = scrollOffset;
    scrollOffset = std::clamp(scrollOffset + lines, 0, screen.scrollbackSize());

    if (scrollOffset != previous)
        repaint();
}

// ---- rendering -------------------------------------------------------------

std::pair<Rgb, Rgb>
    TerminalView::effectiveColors(const Cell& cell, long absoluteRow, int col) const
{
    auto fg = cell.fg;
    auto bg = cell.bg;

    if ((cell.attrs & Attr::Inverse) != 0)
        std::swap(fg, bg);

    if (isSelected(absoluteRow, col))
        bg = theme.selection;

    return {fg, bg};
}

void TerminalView::drawBackgrounds(int visualRow, const Line& line, float y)
{
    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();
    const auto absoluteRow =
        (long) screen.scrollbackSize() - scrollOffset + visualRow;

    const auto cols = std::min((int) line.size(), screen.columns());
    auto runStart = 0;
    auto runBg = Rgb {};
    auto runActive = false;

    auto flush = [&](int endCol)
    {
        if (!runActive)
            return;

        sprites->fillRect({marginX + (float) runStart * cellW,
                           y,
                           (float) (endCol - runStart) * cellW,
                           cellH},
                          toColor(runBg));
        runActive = false;
    };

    for (auto col = 0; col < cols; ++col)
    {
        const auto& cell = line[(std::size_t) col];
        const auto [fg, bg] = effectiveColors(cell, absoluteRow, col);

        const auto isDefault = bg == theme.background
                               && (cell.attrs & Attr::Inverse) == 0
                               && !isSelected(absoluteRow, col);

        if (isDefault)
        {
            flush(col);
            continue;
        }

        if (runActive && bg == runBg)
            continue;

        flush(col);
        runStart = col;
        runBg = bg;
        runActive = true;
    }

    flush(cols);
}

void TerminalView::drawGlyphs(int visualRow, const Line& line, float y)
{
    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();
    const auto baseline = atlas->baseline();
    const auto absoluteRow =
        (long) screen.scrollbackSize() - scrollOffset + visualRow;

    const auto cols = std::min((int) line.size(), screen.columns());

    for (auto col = 0; col < cols; ++col)
    {
        const auto& cell = line[(std::size_t) col];

        if ((cell.attrs & Attr::WideCont) != 0)
            continue;

        if ((cell.attrs & Attr::Hidden) != 0)
            continue;

        if ((cell.attrs & Attr::Blink) != 0 && !blinkOn)
            continue;

        const auto [fg, bg] = effectiveColors(cell, absoluteRow, col);
        const auto x = marginX + (float) col * cellW;
        const auto wide = (cell.attrs & Attr::Wide) != 0;
        const auto width = wide ? cellW * 2 : cellW;

        if (cell.ch != U' ')
        {
            const auto slot = atlas->glyph(cell.ch,
                                           (cell.attrs & Attr::Bold) != 0,
                                           (cell.attrs & Attr::Italic) != 0,
                                           wide ? 2 : 1);

            if (slot.valid && !slot.empty)
            {
                const auto alpha = (cell.attrs & Attr::Faint) != 0 ? 0.55f : 1.0f;
                const auto tint = slot.colored ? Graphics::Color::white(alpha)
                                               : toColor(fg, alpha);

                // Placed by its own bearings from the cell origin and baseline,
                // rather than stretched to fill the cell.
                const auto scale = atlas->scale();

                glyphs->add({x + slot.offset.x,
                             y + baseline + slot.offset.y,
                             slot.src.w / scale,
                             slot.src.h / scale},
                            slot.src,
                            tint,
                            slot.colored);
            }
        }

        if ((cell.attrs & Attr::Underline) != 0)
            sprites->fillRect({x, y + baseline + 1.5f, width, 1.0f}, toColor(fg));

        if ((cell.attrs & Attr::Strike) != 0)
            sprites->fillRect({x, y + cellH * 0.55f, width, 1.0f}, toColor(fg));
    }
}

void TerminalView::drawCursor()
{
    if (!screen.modes.showCursor)
        return;

    const auto visualRow = screen.cursor.y + scrollOffset;

    if (visualRow >= screen.rows())
        return;

    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();
    const auto x = marginX + (float) screen.cursor.x * cellW;
    const auto y = marginY + (float) visualRow * cellH;
    const auto color = toColor(theme.cursor);

    const auto shape = parser.cursorShape();
    const auto blinking = screen.modes.cursorBlink && (shape == 0 || shape % 2 == 1);

    if (!hasFocus())
    {
        sprites->drawRect({x, y, cellW, cellH}, color, 1.0f);
        return;
    }

    if (blinking && !blinkOn)
        return;

    if (shape == 3 || shape == 4)
    {
        sprites->fillRect({x, y + cellH - 2.0f, cellW, 2.0f}, color);
        return;
    }

    if (shape == 5 || shape == 6)
    {
        sprites->fillRect({x, y, 2.0f, cellH}, color);
        return;
    }

    sprites->fillRect({x, y, cellW, cellH}, color);

    // Repaint the glyph under a block cursor in the background color so it
    // stays legible.
    const auto& line = screen.lineAt(visualRow, scrollOffset);

    if (screen.cursor.x < (int) line.size())
    {
        const auto& cell = line[(std::size_t) screen.cursor.x];

        if (cell.ch != U' ' && (cell.attrs & Attr::WideCont) == 0)
        {
            const auto wide = (cell.attrs & Attr::Wide) != 0;
            const auto slot = atlas->glyph(cell.ch,
                                           (cell.attrs & Attr::Bold) != 0,
                                           (cell.attrs & Attr::Italic) != 0,
                                           wide ? 2 : 1);

            if (slot.valid && !slot.empty && !slot.colored)
            {
                const auto scale = atlas->scale();
                const auto baseline = atlas->baseline();

                glyphs->add({x + slot.offset.x,
                             y + baseline + slot.offset.y,
                             slot.src.w / scale,
                             slot.src.h / scale},
                            slot.src,
                            toColor(theme.background),
                            false);
            }
        }
    }
}

void TerminalView::drawCopyMode(const std::string& indicator)
{
    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();
    const auto baseline = atlas->baseline();
    const auto scale = atlas->scale();

    const auto visualRow =
        (int) (copyCursor.row - ((long) screen.scrollbackSize() - scrollOffset));

    if (visualRow >= 0 && visualRow < screen.rows())
    {
        const auto x = marginX + (float) copyCursor.col * cellW;
        const auto y = marginY + (float) visualRow * cellH;

        sprites->fillRect({x, y, cellW, cellH}, toColor(theme.cursor));

        // Like the normal block cursor: repaint the covered glyph in the
        // background color so it stays legible.
        const auto& line = lineAtAbsolute(copyCursor.row);

        if (copyCursor.col < (int) line.size())
        {
            const auto& cell = line[(std::size_t) copyCursor.col];

            if (cell.ch != U' ' && (cell.attrs & Attr::WideCont) == 0)
            {
                const auto slot = atlas->glyph(cell.ch,
                                               (cell.attrs & Attr::Bold) != 0,
                                               (cell.attrs & Attr::Italic) != 0,
                                               (cell.attrs & Attr::Wide) != 0 ? 2
                                                                              : 1);

                if (slot.valid && !slot.empty && !slot.colored)
                    glyphs->add({x + slot.offset.x,
                                 y + baseline + slot.offset.y,
                                 slot.src.w / scale,
                                 slot.src.h / scale},
                                slot.src,
                                toColor(theme.background),
                                false);
            }
        }
    }

    // tmux's position badge, top right: lines scrolled up / history size.
    const auto pad = 4.0f;
    const auto width = (float) indicator.size() * cellW + pad * 2;
    const auto x0 = getLocalBounds().w - width - marginX;

    sprites->fillRect({x0, marginY, width, cellH}, toColor(theme.ansi[3]));

    auto x = x0 + pad;

    for (const auto c: indicator)
    {
        const auto slot = atlas->glyph((char32_t) c, false, false, 1);

        if (slot.valid && !slot.empty)
            glyphs->add({x + slot.offset.x,
                         marginY + baseline + slot.offset.y,
                         slot.src.w / scale,
                         slot.src.h / scale},
                        slot.src,
                        toColor(theme.background),
                        false);

        x += cellW;
    }
}

void TerminalView::render(GPU::Frame& frame)
{
    auto pass = frame.beginPass({toColor(theme.background)});

    if (!sprites)
        return;

    sprites->begin(pass);
    glyphs->begin();

    const auto rows = screen.rows();
    const auto cellH = atlas->cellHeight();

    // Rasterize every glyph this frame needs before the first draw call so
    // the atlas texture uploads once, ahead of the draws that sample it.
    for (auto row = 0; row < rows; ++row)
    {
        const auto& line = screen.lineAt(row, scrollOffset);
        const auto cols = std::min((int) line.size(), screen.columns());

        for (auto col = 0; col < cols; ++col)
        {
            const auto& cell = line[(std::size_t) col];

            if (cell.ch != U' '
                && (cell.attrs & (Attr::WideCont | Attr::Hidden)) == 0)
                atlas->glyph(cell.ch,
                             (cell.attrs & Attr::Bold) != 0,
                             (cell.attrs & Attr::Italic) != 0,
                             (cell.attrs & Attr::Wide) != 0 ? 2 : 1);
        }
    }

    auto copyIndicator = std::string {};

    if (copyMode)
    {
        copyIndicator = "[" + std::to_string(scrollOffset) + "/"
                        + std::to_string(screen.scrollbackSize()) + "]";

        for (const auto c: copyIndicator)
            atlas->glyph((char32_t) c, false, false, 1);
    }

    atlas->commit();

    for (auto row = 0; row < rows; ++row)
        drawBackgrounds(
            row, screen.lineAt(row, scrollOffset), marginY + (float) row * cellH);

    for (auto row = 0; row < rows; ++row)
        drawGlyphs(
            row, screen.lineAt(row, scrollOffset), marginY + (float) row * cellH);

    // Every cell's glyph is queued by now, so the whole screen submits as one
    // instanced draw rather than one call per character.
    glyphs->flush(pass, atlas->atlas());

    if (copyMode || scrollOffset == 0)
    {
        // The cursor draws a filled block through the sprite renderer and then
        // the glyph on top of it, so both pipelines have to be rebound: the
        // glyph flush above left its own pipeline in place.
        sprites->begin(pass);
        glyphs->begin();

        if (copyMode)
            drawCopyMode(copyIndicator);
        else
            drawCursor();

        glyphs->flush(pass, atlas->atlas());
    }

    renderedVersion = screen.version();
}

// ---- keyboard --------------------------------------------------------------

bool TerminalView::handleCommandShortcut(const KeyEvent& event)
{
    if (!event.modifiers.command)
        return false;

    const auto& chars = event.charactersIgnoringModifiers;

    if (chars == "c")
    {
        copySelection();
        return true;
    }

    if (chars == "v")
    {
        paste();
        return true;
    }

    if (chars == "k")
    {
        screen.eraseInDisplay(3);
        screen.setCursor(0, 0);
        scrollOffset = 0;
        send("\014");
        repaint();
        return true;
    }

    return true;
}

bool TerminalView::handleSpecialKey(const KeyEvent& event)
{
    const auto& mods = event.modifiers;
    const auto app = screen.modes.appCursorKeys && modifierCode(mods) == 1;

    switch (event.keyCode)
    {
        case KeyCode::Return:
        case MacKey::KeypadEnter:
            sendAndScrollToBottom("\r");
            return true;

        case KeyCode::Tab:
            sendAndScrollToBottom(mods.shift ? "\033[Z" : "\t");
            return true;

        case KeyCode::Escape:
            sendAndScrollToBottom("\033");
            return true;

        case KeyCode::Delete:
            sendAndScrollToBottom(mods.alt ? "\033\x7f" : "\x7f");
            return true;

        case MacKey::ForwardDelete:
            sendAndScrollToBottom(tildeKey(3, mods));
            return true;

        case KeyCode::UpArrow:
            sendAndScrollToBottom(app ? "\033OA" : csiWithModifiers('A', mods));
            return true;

        case KeyCode::DownArrow:
            sendAndScrollToBottom(app ? "\033OB" : csiWithModifiers('B', mods));
            return true;

        case KeyCode::RightArrow:
            sendAndScrollToBottom(app ? "\033OC" : csiWithModifiers('C', mods));
            return true;

        case KeyCode::LeftArrow:
            sendAndScrollToBottom(app ? "\033OD" : csiWithModifiers('D', mods));
            return true;

        case MacKey::Home:
            if (mods.shift)
                scrollBy(screen.scrollbackSize());
            else
                sendAndScrollToBottom(app ? "\033OH" : "\033[H");
            return true;

        case MacKey::End:
            if (mods.shift)
                scrollBy(-screen.scrollbackSize());
            else
                sendAndScrollToBottom(app ? "\033OF" : "\033[F");
            return true;

        case MacKey::PageUp:
            if (mods.shift)
                scrollBy(screen.rows() - 1);
            else
                sendAndScrollToBottom(tildeKey(5, mods));
            return true;

        case MacKey::PageDown:
            if (mods.shift)
                scrollBy(-(screen.rows() - 1));
            else
                sendAndScrollToBottom(tildeKey(6, mods));
            return true;

        case KeyCode::F1:
            sendAndScrollToBottom("\033OP");
            return true;
        case KeyCode::F2:
            sendAndScrollToBottom("\033OQ");
            return true;
        case KeyCode::F3:
            sendAndScrollToBottom("\033OR");
            return true;
        case KeyCode::F4:
            sendAndScrollToBottom("\033OS");
            return true;
        case KeyCode::F5:
            sendAndScrollToBottom(tildeKey(15, mods));
            return true;
        case KeyCode::F6:
            sendAndScrollToBottom(tildeKey(17, mods));
            return true;
        case KeyCode::F7:
            sendAndScrollToBottom(tildeKey(18, mods));
            return true;
        case KeyCode::F8:
            sendAndScrollToBottom(tildeKey(19, mods));
            return true;
        case KeyCode::F9:
            sendAndScrollToBottom(tildeKey(20, mods));
            return true;
        case KeyCode::F10:
            sendAndScrollToBottom(tildeKey(21, mods));
            return true;
        case KeyCode::F11:
            sendAndScrollToBottom(tildeKey(23, mods));
            return true;
        case KeyCode::F12:
            sendAndScrollToBottom(tildeKey(24, mods));
            return true;

        default:
            return false;
    }
}

void TerminalView::keyDown(const KeyEvent& event)
{
    if (interceptKey(event))
        return;

    if (copyMode && handleCopyModeKey(event))
        return;

    if (handleCommandShortcut(event))
        return;

    if (handleSpecialKey(event))
        return;

    const auto& mods = event.modifiers;

    if (mods.control && event.charactersIgnoringModifiers == " ")
    {
        sendAndScrollToBottom(std::string_view {"\0", 1});
        return;
    }

    if (mods.alt && !event.charactersIgnoringModifiers.empty())
    {
        sendAndScrollToBottom("\033" + event.charactersIgnoringModifiers);
        return;
    }

    if (!event.characters.empty())
        sendAndScrollToBottom(event.characters);
}

void TerminalView::paste()
{
    auto text = Clipboard::getText();

    if (text.empty())
        return;

    auto normalized = std::string {};
    normalized.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            continue;

        normalized.push_back(text[i] == '\n' ? '\r' : text[i]);
    }

    if (screen.modes.bracketedPaste)
        normalized = "\033[200~" + normalized + "\033[201~";

    sendAndScrollToBottom(normalized);
}

// ---- selection & mouse -----------------------------------------------------

TerminalView::CellRef TerminalView::cellRefAt(Graphics::Point pos) const
{
    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();

    const auto col =
        std::clamp((int) ((pos.x - marginX) / cellW), 0, screen.columns() - 1);
    const auto visualRow =
        std::clamp((int) ((pos.y - marginY) / cellH), 0, screen.rows() - 1);

    return {(long) screen.scrollbackSize() - scrollOffset + visualRow, col};
}

bool TerminalView::hasSelection() const
{
    return selectionActive && !(selectionStart == selectionEnd && !selecting);
}

bool TerminalView::isSelected(long absoluteRow, int col) const
{
    if (!selectionActive)
        return false;

    const auto cell = CellRef {absoluteRow, col};
    return !(cell < selectionStart) && !(selectionEnd < cell);
}

std::string TerminalView::selectedText() const
{
    if (!selectionActive)
        return {};

    auto result = std::string {};

    for (auto row = selectionStart.row; row <= selectionEnd.row; ++row)
    {
        // Grid rows sit past the scrollback and resolve to negative offsets,
        // down to -(rows - 1) for the bottom line.
        const auto offset = (int) (screen.scrollbackSize() - row);

        if (offset <= -screen.rows() || offset > screen.scrollbackSize())
            continue;

        const auto& line = screen.lineAt(0, offset);
        const auto fromCol = row == selectionStart.row ? selectionStart.col : 0;
        const auto toCol =
            row == selectionEnd.row ? selectionEnd.col : (int) line.size() - 1;

        auto text = std::string {};

        for (auto col = fromCol; col <= std::min(toCol, (int) line.size() - 1);
             ++col)
        {
            const auto& cell = line[(std::size_t) col];

            if ((cell.attrs & Attr::WideCont) != 0)
                continue;

            appendUtf8(text, cell.ch);
        }

        while (!text.empty() && text.back() == ' ')
            text.pop_back();

        result += text;

        if (row != selectionEnd.row)
            result.push_back('\n');
    }

    return result;
}

void TerminalView::copySelection()
{
    if (hasSelection())
        Clipboard::copyText(selectedText());
}

// ---- copy mode -------------------------------------------------------------

const Line& TerminalView::lineAtAbsolute(long row) const
{
    return screen.lineAt(0, (int) ((long) screen.scrollbackSize() - row));
}

int TerminalView::lineLength(long row) const
{
    const auto& line = lineAtAbsolute(row);
    auto length = (int) line.size();

    while (length > 0 && line[(std::size_t) length - 1].ch == U' ')
        --length;

    return length;
}

void TerminalView::enterCopyMode()
{
    if (copyMode)
        return;

    copyMode = true;
    copySelecting = false;
    copyLineSelect = false;

    // Already scrolled up: keep the view and start at its top-left, like
    // tmux. Otherwise start at the terminal cursor.
    if (scrollOffset > 0)
        setCopyCursor((long) screen.scrollbackSize() - scrollOffset, 0);
    else
        setCopyCursor((long) screen.scrollbackSize() + screen.cursor.y,
                      screen.cursor.x);
}

void TerminalView::exitCopyMode()
{
    copyMode = false;
    copySelecting = false;
    copyLineSelect = false;
    selectionActive = false;
    scrollOffset = 0;
    repaint();
}

void TerminalView::setCopyCursor(long row, int col)
{
    const auto lastRow = (long) screen.scrollbackSize() + screen.rows() - 1;
    copyCursor.row = std::clamp(row, 0L, lastRow);
    copyCursor.col = std::clamp(col, 0, screen.columns() - 1);

    const auto visualRow =
        (int) (copyCursor.row - ((long) screen.scrollbackSize() - scrollOffset));

    if (visualRow < 0)
        scrollOffset = (int) ((long) screen.scrollbackSize() - copyCursor.row);
    else if (visualRow >= screen.rows())
        scrollOffset = (int) ((long) screen.scrollbackSize() - copyCursor.row
                              + screen.rows() - 1);

    scrollOffset = std::clamp(scrollOffset, 0, screen.scrollbackSize());
    updateCopySelection();
    repaint();
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

    selectionStart = start;
    selectionEnd = end;
    selectionActive = true;
}

void TerminalView::moveCopyCursorByWord(bool forward)
{
    const auto lastRow = (long) screen.scrollbackSize() + screen.rows() - 1;
    auto row = copyCursor.row;
    auto col = copyCursor.col;

    const auto charAt = [&](long r, int c) -> char32_t
    {
        const auto& line = lineAtAbsolute(r);
        return c < (int) line.size() ? line[(std::size_t) c].ch : U' ';
    };

    if (forward)
    {
        const auto step = [&]
        {
            if (col < screen.columns() - 1)
            {
                ++col;
                return true;
            }

            if (row >= lastRow)
                return false;

            ++row;
            col = 0;
            return true;
        };

        // Off the current word, then over the gap to the next one.
        while (charAt(row, col) != U' ' && step()) {}
        while (charAt(row, col) == U' ' && step()) {}
    }
    else
    {
        const auto step = [&]
        {
            if (col > 0)
            {
                --col;
                return true;
            }

            if (row <= 0)
                return false;

            --row;
            col = screen.columns() - 1;
            return true;
        };

        // Over the gap backwards, then to the head of that word.
        if (step())
        {
            while (charAt(row, col) == U' ' && step()) {}

            while (col > 0 && charAt(row, col - 1) != U' ')
                --col;
        }
    }

    setCopyCursor(row, col);
}

bool TerminalView::handleCopyModeKey(const KeyEvent& event)
{
    // Cmd shortcuts (zoom, copy, paste) stay live; the mode consumes
    // everything else so no keystroke leaks into the shell.
    if (event.modifiers.command)
        return false;

    const auto& chars = event.charactersIgnoringModifiers;
    const auto rows = screen.rows();

    const auto move = [&](long deltaRow, int deltaCol)
    { setCopyCursor(copyCursor.row + deltaRow, copyCursor.col + deltaCol); };

    const auto beginSelection = [&](bool lines)
    {
        copySelecting = true;
        copyLineSelect = lines;
        copyAnchor = copyCursor;
        updateCopySelection();
        repaint();
    };

    if (event.keyCode == KeyCode::Escape)
    {
        // First Escape drops the selection, the second leaves the mode.
        if (copySelecting)
        {
            copySelecting = false;
            copyLineSelect = false;
            selectionActive = false;
            repaint();
        }
        else
        {
            exitCopyMode();
        }

        return true;
    }

    if (chars == "q" || (event.modifiers.control && chars == "c"))
    {
        exitCopyMode();
        return true;
    }

    if (chars == "y" || event.keyCode == KeyCode::Return
        || event.keyCode == MacKey::KeypadEnter)
    {
        if (selectionActive)
            Clipboard::copyText(selectedText());

        exitCopyMode();
        return true;
    }

    if (event.modifiers.control)
    {
        if (chars == "u")
            move(-(long) (rows / 2), 0);
        else if (chars == "d")
            move(rows / 2, 0);
        else if (chars == "b")
            move(-(long) (rows - 1), 0);
        else if (chars == "f")
            move(rows - 1, 0);

        return true;
    }

    if (event.keyCode == MacKey::PageUp)
    {
        move(-(long) (rows - 1), 0);
        return true;
    }

    if (event.keyCode == MacKey::PageDown)
    {
        move(rows - 1, 0);
        return true;
    }

    if (chars == "h" || event.keyCode == KeyCode::LeftArrow)
        move(0, -1);
    else if (chars == "j" || event.keyCode == KeyCode::DownArrow)
        move(1, 0);
    else if (chars == "k" || event.keyCode == KeyCode::UpArrow)
        move(-1, 0);
    else if (chars == "l" || event.keyCode == KeyCode::RightArrow)
        move(0, 1);
    else if (chars == "0" || event.keyCode == MacKey::Home)
        setCopyCursor(copyCursor.row, 0);
    else if (chars == "$" || event.keyCode == MacKey::End)
        setCopyCursor(copyCursor.row, std::max(0, lineLength(copyCursor.row) - 1));
    else if (chars == "g")
        setCopyCursor(0, 0);
    else if (chars == "G")
        setCopyCursor((long) screen.scrollbackSize() + rows - 1, 0);
    else if (chars == "w")
        moveCopyCursorByWord(true);
    else if (chars == "b")
        moveCopyCursorByWord(false);
    else if (chars == "v" || chars == " ")
        beginSelection(false);
    else if (chars == "V")
        beginSelection(true);

    return true;
}

bool TerminalView::mouseReportingActive() const
{
    const auto& modes = screen.modes;
    return modes.mouseButtons || modes.mouseDrag || modes.mouseMotion;
}

void TerminalView::sendMouseReport(const MouseEvent& event,
                                   int button,
                                   bool pressed,
                                   bool motion)
{
    if (!screen.modes.mouseSgr)
        return;

    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();
    const auto col =
        std::clamp((int) ((event.pos.x - marginX) / cellW) + 1, 1, screen.columns());
    const auto row =
        std::clamp((int) ((event.pos.y - marginY) / cellH) + 1, 1, screen.rows());

    char buffer[32];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "\033[<%d;%d;%d%c",
                  button + (motion ? 32 : 0),
                  col,
                  row,
                  pressed ? 'M' : 'm');
    send(buffer);
}

void TerminalView::mouseDown(const MouseEvent& event)
{
    onFocused();

    if (mouseReportingActive() && !event.modifiers.shift)
    {
        sendMouseReport(event, (int) event.button, true, false);
        return;
    }

    if (event.button != Graphics::MouseButton::Left)
        return;

    const auto cell = cellRefAt(event.pos);

    if (event.clickCount >= 3)
    {
        selectionStart = {cell.row, 0};
        selectionEnd = {cell.row, screen.columns() - 1};
        selectionActive = true;
        selecting = false;
        repaint();
        return;
    }

    if (event.clickCount == 2)
    {
        const auto offset = (int) (screen.scrollbackSize() - cell.row);
        const auto& line = screen.lineAt(0, offset);

        auto isWordChar = [&](int col)
        {
            if (col < 0 || col >= (int) line.size())
                return false;

            const auto ch = line[(std::size_t) col].ch;
            return ch != U' ';
        };

        auto from = cell.col;
        auto to = cell.col;

        while (isWordChar(from - 1))
            --from;

        while (isWordChar(to + 1))
            ++to;

        selectionStart = {cell.row, from};
        selectionEnd = {cell.row, to};
        selectionActive = isWordChar(cell.col);
        selecting = false;
        repaint();
        return;
    }

    selectionAnchor = cell;
    selectionStart = cell;
    selectionEnd = cell;
    selectionActive = false;
    selecting = true;
    repaint();
}

void TerminalView::mouseDragged(const MouseEvent& event)
{
    if (mouseReportingActive() && !event.modifiers.shift)
    {
        if (screen.modes.mouseDrag || screen.modes.mouseMotion)
            sendMouseReport(event, (int) event.button, true, true);

        return;
    }

    if (!selecting)
        return;

    const auto cell = cellRefAt(event.pos);
    selectionStart = std::min(selectionAnchor, cell);
    selectionEnd = std::max(selectionAnchor, cell);
    selectionActive = true;
    repaint();
}

void TerminalView::mouseUp(const MouseEvent& event)
{
    if (mouseReportingActive() && !event.modifiers.shift)
    {
        sendMouseReport(event, (int) event.button, false, false);
        return;
    }

    if (selecting && selectionStart == selectionEnd)
        selectionActive = false;

    selecting = false;
    repaint();
}

void TerminalView::mouseWheel(const MouseEvent& event)
{
    if (mouseReportingActive() && !event.modifiers.shift)
    {
        const auto up = event.delta.y > 0;
        sendMouseReport(event, up ? 64 : 65, true, false);
        return;
    }

    wheelRemainder += event.delta.y;
    const auto lines = (int) wheelRemainder;

    if (lines == 0)
        return;

    wheelRemainder -= (float) lines;

    // Full-screen apps get wheel motion as arrow keys, the classic fallback
    // when no mouse mode is active.
    if (screen.modes.altScreen)
    {
        const auto key = lines > 0
                             ? (screen.modes.appCursorKeys ? "\033OA" : "\033[A")
                             : (screen.modes.appCursorKeys ? "\033OB" : "\033[B");

        for (auto i = 0; i < std::min(std::abs(lines), 40); ++i)
            send(key);

        return;
    }

    scrollBy(lines);
}
} // namespace term
