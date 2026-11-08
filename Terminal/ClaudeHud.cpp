#include "ClaudeHud.h"

#include "TextLayout.h"

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
constexpr auto listMetrics = ListPanelMetrics {.maximumWidth = 760.0f,
                                               .rowHeight = 48.0f,
                                               .headerHeight = 46.0f,
                                               .hintHeight = 22.0f,
                                               .maximumRows = 10,
                                               .topFraction = 0.14f};
constexpr auto fontSizes = OverlayFontSizes {.header = 16.0f, .row = 14.0f, .detail = 12.0f};
constexpr auto refreshIntervalMs = 1000;

constexpr auto textLeft = 18.0f;
constexpr auto headerBaseline = 30.0f;
constexpr auto summaryRightPadding = 16.0f;
constexpr auto rowSideInset = 8.0f;
constexpr auto rowGap = 4.0f;
constexpr auto firstLineFraction = 0.40f;
constexpr auto secondLineFraction = 0.78f;
constexpr auto iconLeft = 12.0f;
constexpr auto textAfterIcon = 36.0f;
constexpr auto ageRightPadding = 12.0f;
constexpr auto sessionNameLength = 24;
constexpr auto detailLength = 84;
constexpr auto emptyMessageFraction = 0.55f;
constexpr auto hintBottomOffset = 10.0f;

constexpr auto secondsPerMinute = 60;
constexpr auto secondsPerHour = 3600;
constexpr auto secondsPerDay = 86400;

std::string formatAge(std::int64_t seconds)
{
    if (seconds < secondsPerMinute)
        return "now";

    if (seconds < secondsPerHour)
        return std::to_string(seconds / secondsPerMinute) + "m";

    if (seconds < secondsPerDay)
        return std::to_string(seconds / secondsPerHour) + "h";

    return std::to_string(seconds / secondsPerDay) + "d";
}

bool isListedBefore(const auto& a, const auto& b)
{
    if (a.attention != b.attention)
        return a.attention;

    if (a.attention && a.notifyAt != b.notifyAt)
        return a.notifyAt > b.notifyAt;

    return a.lastUsed > b.lastUsed;
}
} // namespace

ClaudeHud::ClaudeHud(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : OverlayPanel(configToUse, sessionsToUse, fontSizes)
{
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
        refreshIntervalMs);

    repaint();
}

void ClaudeHud::rebuild()
{
    const auto* previous =
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
            item.cwd = compactHomePath(pane->workingDirectory());
            item.notify = session->lastNotify;
            item.notifyAt = session->lastNotifyAt;
            item.attention = session->notifyUnseen;
            item.lastUsed = sessions.lastUsed(item.sessionKey);
            items.push_back(std::move(item));
        }
    }

    sortByAttentionThenRecency();
    keepSelectionOn(previous);

    selected = std::clamp(selected, 0, std::max((int) items.size() - 1, 0));
}

// Waiting-on-you first (newest ping up top), then by session recency.
void ClaudeHud::sortByAttentionThenRecency()
{
    std::stable_sort(items.begin(),
                     items.end(),
                     [](const auto& a, const auto& b) { return isListedBefore(a, b); });
}

// Rows reorder underneath us as notifications arrive.
void ClaudeHud::keepSelectionOn(const TerminalView* pane)
{
    if (pane == nullptr)
        return;

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].pane == pane)
        {
            selected = (int) i;
            return;
        }
    }
}

void ClaudeHud::chooseSelected()
{
    if (selected >= (int) items.size())
    {
        dismiss();
        return;
    }

    const auto item = items[(std::size_t) selected];
    auto* session = sessions.find(item.sessionKey);

    if (session == nullptr)
    {
        dismiss();
        return;
    }

    shown = false;
    tick.reset();
    onClosed();
    sessions.switchTo(*session);

    // Land on the pane that owns the conversation, not just the session --
    // but only if it still exists (a shell may have exited under the HUD).
    const auto panes = session->view.claudePanes();

    if (std::find(panes.begin(), panes.end(), item.pane) != panes.end())
        session->view.focusPane(item.pane);
}

void ClaudeHud::dismiss()
{
    shown = false;
    tick.reset();
    onClosed();
}

void ClaudeHud::moveSelection(int delta)
{
    if (!items.empty())
    {
        selected = wrapSelection(selected, delta, (int) items.size());
        repaint();
    }
}

bool ClaudeHud::keyDown(const KeyEvent& event)
{
    const auto& chars = event.charactersIgnoringModifiers;

    if (event.keyCode == KeyCode::Escape)
        dismiss();
    else if (event.keyCode == KeyCode::Return)
        chooseSelected();
    else if (isSelectPreviousKey(event) || chars == "k")
        moveSelection(-1);
    else if (isSelectNextKey(event) || chars == "j")
        moveSelection(1);

    // Everything else is swallowed while the HUD owns the keyboard.
    return true;
}

Rect ClaudeHud::calculatePanelBounds() const
{
    return calculateListPanelBounds(getLocalBounds(), listMetrics, (int) items.size());
}

int ClaudeHud::findRowAt(Point pos) const
{
    return findListRowAt(calculatePanelBounds(), listMetrics, (int) items.size(), pos);
}

void ClaudeHud::mouseMove(const MouseEvent& event)
{
    if (const auto row = findRowAt(event.position); row >= 0 && row != selected)
    {
        selected = row;
        repaint();
    }
}

int ClaudeHud::countWaitingInstances() const
{
    return (int) std::count_if(
        items.begin(), items.end(), [](const auto& item) { return item.attention; });
}

std::string ClaudeHud::calculateSummary() const
{
    auto summary =
        std::to_string(items.size()) + (items.size() == 1 ? " instance" : " instances");

    if (const auto waiting = countWaitingInstances(); waiting > 0)
        summary += " · " + std::to_string(waiting) + " waiting";

    return summary;
}

// The roster in one line: how many are running, how many wait.
void ClaudeHud::paintHeader(UI::Graphics& g, Rect panel)
{
    const auto baseline = panel.y + headerBaseline;

    g.setColour(toColor(theme.ansi[5]));
    drawTextInFont(g, "✳ claude", {panel.x + textLeft, baseline}, headerFont);

    const auto summary = calculateSummary();
    const auto summaryWidth = measureTextInFont(g, summary, detailFont);
    const auto summaryColour = countWaitingInstances() > 0 ? theme.ansi[3] : theme.ansi[8];
    g.setColour(toColor(summaryColour));
    drawTextInFont(g,
                   summary,
                   {panel.right() - summaryWidth - summaryRightPadding, baseline},
                   detailFont);

    paintHeaderDivider(g, panel, listMetrics.headerHeight);
}

void ClaudeHud::paintRow(UI::Graphics& g, const Item& item, Rect rowArea, std::int64_t now)
{
    const auto line1 = rowArea.y + listMetrics.rowHeight * firstLineFraction;
    const auto line2 = rowArea.y + listMetrics.rowHeight * secondLineFraction;
    const auto attentionColour = item.attention ? theme.ansi[3] : theme.ansi[5];

    g.setColour(toColor(attentionColour));
    drawTextInFont(g, "✳", {rowArea.x + iconLeft, line1}, rowFont);

    g.setColour(toColor(theme.foreground));
    drawTextInFont(g,
                   truncateWithEllipsis(item.sessionName, sessionNameLength),
                   {rowArea.x + textAfterIcon, line1},
                   rowFont);

    // Right of line 1: the last notification's age, loud while unseen.
    if (item.notifyAt > 0)
    {
        const auto age = formatAge(now - item.notifyAt);
        const auto text = item.attention ? "● " + age : age;
        const auto width = measureTextInFont(g, text, detailFont);
        const auto ageColour = item.attention ? theme.ansi[3] : theme.ansi[8];
        g.setColour(toColor(ageColour));
        drawTextInFont(
            g, text, {rowArea.right() - width - ageRightPadding, line1}, detailFont);
    }

    // Line 2: conversation title (falling back to the cwd), and the last
    // notification text while it's waiting to be seen.
    auto detail = item.title.empty() ? item.cwd : item.title;

    if (item.attention && !item.notify.empty())
        detail += "  ·  " + item.notify;

    g.setColour(toColor(theme.ansi[8]));
    drawTextInFont(g,
                   truncateWithEllipsis(detail, detailLength),
                   {rowArea.x + textAfterIcon, line2},
                   detailFont);
}

void ClaudeHud::paint(UI::Graphics& g)
{
    const auto panel = calculatePanelBounds();
    const auto now = (std::int64_t) std::time(nullptr);

    paintListBackdropAndCard(g, panel);
    paintHeader(g, panel);

    const auto rows = countVisibleListRows(listMetrics, (int) items.size());

    for (auto i = 0; i < rows; ++i)
    {
        const auto rowArea = Rect {panel.x + rowSideInset,
                                   calculateListRowTop(panel, listMetrics, i),
                                   panel.w - 2.0f * rowSideInset,
                                   listMetrics.rowHeight - rowGap};

        if (i == selected)
            paintSelectedRowHighlight(g, rowArea);

        paintRow(g, items[(std::size_t) i], rowArea, now);
    }

    g.setColour(toColor(theme.ansi[8]));

    if (items.empty())
    {
        const auto emptyBaseline =
            panel.y + listMetrics.headerHeight + listMetrics.rowHeight * emptyMessageFraction;
        drawTextInFont(
            g, "no claude instances running", {panel.x + textLeft, emptyBaseline}, rowFont);
    }

    drawTextInFont(g,
                   "enter jump to conversation · esc close",
                   {panel.x + textLeft, panel.bottom() - hintBottomOffset},
                   detailFont);
}
} // namespace term
