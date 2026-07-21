#include "ScreenSerializer.h"

#include <algorithm>
#include <cstdio>

namespace term::web
{
namespace
{
// Attribute bits that change how a cell is drawn; Wide/WideCont are layout
// bookkeeping and DefaultFg/DefaultBg are folded into the color comparison.
constexpr std::uint16_t styleMask = Attr::Bold | Attr::Faint | Attr::Italic
                                    | Attr::Underline | Attr::Blink
                                    | Attr::Inverse | Attr::Hidden | Attr::Strike;

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

void appendColor(std::string& out, int selector, Rgb color)
{
    char buffer[24];
    std::snprintf(buffer,
                  sizeof(buffer),
                  ";%d;2;%d;%d;%d",
                  selector,
                  (int) ((color >> 16) & 0xff),
                  (int) ((color >> 8) & 0xff),
                  (int) (color & 0xff));
    out += buffer;
}

void appendSgr(std::string& out, const Cell& cell)
{
    out += "\x1b[0";

    constexpr std::pair<std::uint16_t, int> pairs[] = {{Attr::Bold, 1},
                                                       {Attr::Faint, 2},
                                                       {Attr::Italic, 3},
                                                       {Attr::Underline, 4},
                                                       {Attr::Blink, 5},
                                                       {Attr::Inverse, 7},
                                                       {Attr::Hidden, 8},
                                                       {Attr::Strike, 9}};

    for (const auto& [bit, code]: pairs)
    {
        if (cell.attrs & bit)
        {
            out.push_back(';');
            out += std::to_string(code);
        }
    }

    if (!(cell.attrs & Attr::DefaultFg))
        appendColor(out, 38, cell.fg);

    if (!(cell.attrs & Attr::DefaultBg))
        appendColor(out, 48, cell.bg);

    out.push_back('m');
}

bool sameStyle(const Cell& a, const Cell& b)
{
    if ((a.attrs & styleMask) != (b.attrs & styleMask))
        return false;

    const auto fgMatches = (a.attrs & Attr::DefaultFg)
                               ? (b.attrs & Attr::DefaultFg)
                               : (!(b.attrs & Attr::DefaultFg) && a.fg == b.fg);
    const auto bgMatches = (a.attrs & Attr::DefaultBg)
                               ? (b.attrs & Attr::DefaultBg)
                               : (!(b.attrs & Attr::DefaultBg) && a.bg == b.bg);

    return fgMatches && bgMatches;
}

// A cell the terminal would paint identically if simply left untouched
// after an erase — safe to drop from the end of a line.
bool isTrailingBlank(const Cell& cell)
{
    return cell.ch == U' ' && (cell.attrs & Attr::DefaultBg)
           && !(cell.attrs & (Attr::Underline | Attr::Strike | Attr::Inverse));
}

void appendLine(std::string& out, const Line& line)
{
    auto end = line.size();

    while (end > 0 && isTrailingBlank(line[end - 1]))
        --end;

    const Cell* previous = nullptr;

    for (std::size_t i = 0; i < end; ++i)
    {
        const auto& cell = line[i];

        if (cell.attrs & Attr::WideCont)
            continue;

        if (previous == nullptr || !sameStyle(*previous, cell))
            appendSgr(out, cell);

        appendUtf8(out, cell.ch);
        previous = &cell;
    }
}

void appendGrid(std::string& out, const TermScreen& screen)
{
    for (auto row = 0; row < screen.rows(); ++row)
    {
        if (row > 0)
            out += "\r\n";

        appendLine(out, screen.lineAt(row, 0));
    }
}
} // namespace

std::string serializeScreen(const TermScreen& screen, int scrollbackLines)
{
    auto out = std::string {};
    out.reserve(16 * 1024);

    // Wipe whatever the receiving terminal held, scrollback included, so a
    // re-attach into a reused xterm instance starts clean.
    out += "\x1b[0m\x1b[?1049l\x1b[3J\x1b[2J\x1b[H";

    if (screen.modes.altScreen)
    {
        // The primary grid is unreachable behind the alt screen (the model
        // only exposes the active grid), so an alt-screen pane reattaches
        // with empty primary history — a documented v1 limit.
        out += "\x1b[?1049h";
        appendGrid(out, screen);
    }
    else
    {
        const auto available = screen.scrollbackSize();
        const auto replay = std::min(available, std::max(0, scrollbackLines));

        // lineAt with an offset of the full scrollback depth indexes the
        // scrollback deque from its oldest line.
        for (auto i = available - replay; i < available; ++i)
        {
            appendLine(out, screen.lineAt(i, available));
            out += "\r\n";
        }

        appendGrid(out, screen);
    }

    out += "\x1b[0m";

    char cursor[24];
    std::snprintf(cursor,
                  sizeof(cursor),
                  "\x1b[%d;%dH",
                  screen.cursor.y + 1,
                  screen.cursor.x + 1);
    out += cursor;

    // Replays the modes that shape what the client should send back
    // (arrows, paste, mouse) or how the cursor shows.
    const auto& modes = screen.modes;

    if (modes.appCursorKeys)
        out += "\x1b[?1h";

    if (modes.appKeypad)
        out += "\x1b=";

    if (modes.bracketedPaste)
        out += "\x1b[?2004h";

    if (modes.mouseButtons)
        out += "\x1b[?1000h";

    if (modes.mouseDrag)
        out += "\x1b[?1002h";

    if (modes.mouseMotion)
        out += "\x1b[?1003h";

    if (modes.mouseSgr)
        out += "\x1b[?1006h";

    if (!modes.autowrap)
        out += "\x1b[?7l";

    if (!modes.showCursor)
        out += "\x1b[?25l";

    return out;
}
} // namespace term::web
