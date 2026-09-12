#include "ClaudeHud.h"


#include <algorithm>
#include <ctime>

namespace term
{
using namespace eacp;
using UI::Color;
using UI::KeyEvent;
using UI::MouseEvent;
using UI::Point;
using UI::Rect;
namespace KeyCode = UI::KeyCode;

namespace
{
// The painter measures and draws in the face currently in force, so these keep
// the old call shape -- text, position, font -- rather than making every call
// site set the font first.
void drawText(UI::Graphics& g,
              const std::string& text,
              UI::Point position,
              const UI::Font& font)
{
    g.setFont(font);
    g.drawText(text, position);
}

float measureWidth(UI::Graphics& g, const std::string& text, const UI::Font& font)
{
    g.setFont(font);
    return g.measureText(text);
}
constexpr float panelWidth = 760.0f;
constexpr float rowHeight = 48.0f;
constexpr float headerHeight = 46.0f;
constexpr float hintHeight = 22.0f;
constexpr int maxRows = 10;

std::string compactPath(std::string path)
{
    const auto home = FilePath::homeDirectory().str();

    if (!home.empty() && path.starts_with(home))
        path = "~" + path.substr(home.size());

    return path;
}

std::string truncated(const std::string& text, std::size_t max)
{
    if (text.size() <= max)
        return text;

    return text.substr(0, max - 1) + "…";
}

std::string formatAge(std::int64_t seconds)
{
    if (seconds < 60)
        return "now";

    if (seconds < 3600)
        return std::to_string(seconds / 60) + "m";

    if (seconds < 86400)
        return std::to_string(seconds / 3600) + "h";

    return std::to_string(seconds / 86400) + "d";
}
} // namespace

ClaudeHud::ClaudeHud(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : config(configToUse)
    , sessions(sessionsToUse)
    , theme(themeByName(configToUse.theme))
    , headerFont({config.font, 16.0f})
    , rowFont({config.font, 14.0f})
    , detailFont({config.font, 12.0f})
{
    setInterceptsMouseClicks(true);
    setWantsKeyboardFocus(true);
}

void ClaudeHud::show()
{
    selected = 0;
    shown = true;
    rebuild();

    tick = std::make_unique<Threads::Timer>(
        [this]
        {
            if (shown)
            {
                rebuild();
                repaint();
            }
        },
        1000);

    repaint();
}

void ClaudeHud::rebuild()
{
    const auto previous =
        selected < (int) items.size() ? items[(std::size_t) selected].pane : nullptr;
    items.clear();

    for (const auto& session: sessions.all())
    {
        for (auto* pane: session->view.claudePanes())
        {
            auto item = Item {};
            item.sessionKey = session->key();
            item.sessionName = session->name;
            item.pane = pane;
            item.title = pane->currentTitle();
            item.cwd = compactPath(pane->workingDirectory());
            item.notify = session->lastNotify;
            item.notifyAt = session->lastNotifyAt;
            item.attention = session->notifyUnseen;
            item.lastUsed = sessions.lastUsed(item.sessionKey);
            items.push_back(std::move(item));
        }
    }

    // Waiting-on-you first (newest ping up top), then by session recency.
    std::stable_sort(items.begin(),
                     items.end(),
                     [](const auto& a, const auto& b)
                     {
                         if (a.attention != b.attention)
                             return a.attention;

                         if (a.attention && a.notifyAt != b.notifyAt)
                             return a.notifyAt > b.notifyAt;

                         return a.lastUsed > b.lastUsed;
                     });

    // Keep the highlight on the same conversation across refreshes; rows
    // reorder underneath us as notifications arrive.
    if (previous != nullptr)
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].pane == previous)
            {
                selected = (int) i;
                break;
            }

    selected = std::clamp(selected, 0, std::max((int) items.size() - 1, 0));
}

void ClaudeHud::choose()
{
    if (selected >= (int) items.size())
    {
        cancel();
        return;
    }

    const auto item = items[(std::size_t) selected];
    auto* session = sessions.find(item.sessionKey);

    if (session == nullptr)
    {
        cancel();
        return;
    }

    shown = false;
    tick.reset();
    onClosed();
    sessions.switchTo(*session);

    // Land on the pane that owns the conversation, not just the session —
    // but only if it still exists (a shell may have exited under the HUD).
    const auto panes = session->view.claudePanes();

    if (std::find(panes.begin(), panes.end(), item.pane) != panes.end())
        session->view.focusPane(item.pane);
}

void ClaudeHud::cancel()
{
    shown = false;
    tick.reset();
    onClosed();
}

void ClaudeHud::moveSelection(int delta)
{
    if (!items.empty())
    {
        selected = (selected + delta + (int) items.size()) % (int) items.size();
        repaint();
    }
}

bool ClaudeHud::keyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        cancel();
        return true;
    }

    if (event.keyCode == KeyCode::Return)
    {
        choose();
        return true;
    }

    const auto& chars = event.charactersIgnoringModifiers;

    if (event.keyCode == KeyCode::UpArrow || chars == "k"
        || (event.modifiers.control && chars == "p"))
    {
        moveSelection(-1);
        return true;
    }

    if (event.keyCode == KeyCode::DownArrow || chars == "j"
        || (event.modifiers.control && chars == "n"))
    {
        moveSelection(1);
        return true;
    }

    // Everything else is swallowed while the HUD owns the keyboard.
}

Rect ClaudeHud::panelBounds() const
{
    const auto bounds = getLocalBounds();
    const auto width = std::min(panelWidth, bounds.w - 60.0f);
    const auto rows = std::min((int) items.size(), maxRows);
    const auto height = headerHeight + (float) std::max(rows, 1) * rowHeight
                        + hintHeight + 12.0f;

    return {
        (bounds.w - width) / 2.0f, std::max(bounds.h * 0.14f, 20.0f), width, height};
}

int ClaudeHud::rowAt(Point pos) const
{
    const auto panel = panelBounds();
    const auto y = pos.y - (panel.y + headerHeight + 4.0f);

    if (pos.x < panel.x || pos.x > panel.right() || y < 0)
        return -1;

    const auto row = (int) (y / rowHeight);
    return row < std::min((int) items.size(), maxRows) ? row : -1;
}

void ClaudeHud::mouseMove(const MouseEvent& event)
{
    if (const auto row = rowAt(event.position); row >= 0 && row != selected)
    {
        selected = row;
        repaint();
    }
}

void ClaudeHud::mouseDown(const MouseEvent& event)
{
    const auto row = rowAt(event.position);

    if (row >= 0)
    {
        selected = row;
        choose();
        return;
    }

    if (!panelBounds().contains(event.position))
        cancel();
}

void ClaudeHud::paint(UI::Graphics& g)
{
    const auto panel = panelBounds();
    const auto now = (std::int64_t) std::time(nullptr);

    g.setColour(Color::black(0.38f));
    g.fillRect(getLocalBounds());

    g.setColour(toColor(theme.background).brighter(0.04f));
    g.fillRoundedRect(panel, 12.0f);

    g.setColour(toColor(theme.selection, 0.8f));
    g.drawRect(panel, 1.0f);

    // Header: the roster in one line — how many are running, how many wait.
    g.setColour(toColor(theme.ansi[5]));
    drawText(g, "✳ claude", {panel.x + 18.0f, panel.y + 30.0f}, headerFont);

    const auto waiting = (int) std::count_if(items.begin(),
                                             items.end(),
                                             [](const auto& item)
                                             { return item.attention; });
    auto summary = std::to_string(items.size())
                   + (items.size() == 1 ? " instance" : " instances");

    if (waiting > 0)
        summary += " · " + std::to_string(waiting) + " waiting";

    const auto summaryWidth =
        measureWidth(g, summary, detailFont);
    g.setColour(waiting > 0 ? toColor(theme.ansi[3]) : toColor(theme.ansi[8]));
    drawText(g, 
        summary, {panel.right() - summaryWidth - 16.0f, panel.y + 30.0f}, detailFont);

    g.setColour(toColor(theme.selection));
    g.drawLine({panel.x + 12.0f, panel.y + headerHeight - 2.0f},
                     {panel.right() - 12.0f, panel.y + headerHeight - 2.0f});

    const auto rows = std::min((int) items.size(), maxRows);

    for (auto i = 0; i < rows; ++i)
    {
        const auto& item = items[(std::size_t) i];
        const auto y = panel.y + headerHeight + 4.0f + (float) i * rowHeight;
        const auto rowRect =
            Rect {panel.x + 8.0f, y, panel.w - 16.0f, rowHeight - 4.0f};

        if (i == selected)
        {
            g.setColour(toColor(theme.selection, 0.85f));
            g.fillRoundedRect(rowRect, 6.0f);
        }

        const auto line1 = y + rowHeight * 0.40f;
        const auto line2 = y + rowHeight * 0.78f;

        g.setColour(item.attention ? toColor(theme.ansi[3])
                                        : toColor(theme.ansi[5]));
        drawText(g, "✳", {rowRect.x + 12.0f, line1}, rowFont);

        g.setColour(toColor(theme.foreground));
        drawText(g, 
            truncated(item.sessionName, 24), {rowRect.x + 36.0f, line1}, rowFont);

        // Right of line 1: the last notification's age, loud while unseen.
        if (item.notifyAt > 0)
        {
            const auto age = formatAge(now - item.notifyAt);
            const auto text = item.attention ? "● " + age : age;
            const auto width = measureWidth(g, text, detailFont);
            g.setColour(item.attention ? toColor(theme.ansi[3])
                                            : toColor(theme.ansi[8]));
            drawText(g, text, {rowRect.right() - width - 12.0f, line1}, detailFont);
        }

        // Line 2: conversation title (falling back to the cwd), and the last
        // notification text while it's waiting to be seen.
        auto detail = item.title.empty() ? item.cwd : item.title;

        if (item.attention && !item.notify.empty())
            detail += "  ·  " + item.notify;

        g.setColour(toColor(theme.ansi[8]));
        drawText(g, truncated(detail, 84), {rowRect.x + 36.0f, line2}, detailFont);
    }

    if (items.empty())
    {
        g.setColour(toColor(theme.ansi[8]));
        drawText(g, 
            "no claude instances running",
            {panel.x + 18.0f, panel.y + headerHeight + rowHeight * 0.55f},
            rowFont);
    }

    g.setColour(toColor(theme.ansi[8]));
    drawText(g, "enter jump to conversation · esc close",
                     {panel.x + 18.0f, panel.bottom() - 10.0f},
                     detailFont);
}
} // namespace term
