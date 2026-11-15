#include "Palette.h"

#include "PaletteRanking.h"
#include "Projects.h"
#include "TextLayout.h"
#include "MruStore.h"
#include "UsageLog.h"

#include <algorithm>

namespace term
{
using namespace eacp;
using UI::KeyEvent;
using UI::MouseEvent;
using UI::Point;
using UI::Rect;

namespace
{
constexpr auto listMetrics = ListPanelMetrics {.maximumWidth = 680.0f,
                                               .rowHeight = 34.0f,
                                               .headerHeight = 46.0f,
                                               .hintHeight = 0.0f,
                                               .maximumRows = 12,
                                               .topFraction = 0.14f};
constexpr auto fontSizes = OverlayFontSizes {.header = 16.0f, .row = 14.0f, .detail = 12.0f};

constexpr auto textLeft = 18.0f;
constexpr auto headerBaseline = 30.0f;

constexpr auto rowSideInset = 8.0f;
constexpr auto rowGap = 2.0f;
constexpr auto baselineFraction = 0.62f;
constexpr auto iconLeft = 12.0f;
constexpr auto labelAfterIcon = 24.0f;
constexpr auto labelLength = 32;
constexpr auto labelToDetailGap = 14.0f;
constexpr auto detailLength = 48;
constexpr auto badgeRightPadding = 12.0f;

bool isSessionItem(const PaletteItem& item)
{
    return item.kind == PaletteItem::Kind::Session;
}

bool isOpenSession(const PaletteItem& item)
{
    return isSessionItem(item) && item.session != nullptr;
}

std::string getItemIcon(const PaletteItem& item)
{
    if (item.claude)
        return "✳";

    if (isSessionItem(item))
        return "●";

    return "▸";
}

Rgb getItemIconColour(const PaletteItem& item, const Theme& theme)
{
    if (item.claude)
        return theme.ansi[5];

    if (isSessionItem(item))
        return theme.ansi[2];

    return theme.ansi[8];
}

std::string describeItem(const PaletteItem& item)
{
    if (!item.status.empty())
        return truncateWithEllipsis(item.status, detailLength);

    return truncateWithEllipsis(item.detail, detailLength);
}

bool isOrderedForEmptyQuery(const PaletteItem& a, const PaletteItem& b)
{
    if (a.kind != b.kind)
        return isSessionItem(a);

    return a.lastUsed > b.lastUsed;
}
} // namespace

Palette::Palette(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : SearchablePanel(configToUse, sessionsToUse, fontSizes)
{
}

void Palette::show()
{
    searchField.reset();
    selected = 0;
    shown = true;
    keysSinceOpen = 0;
    openedAtMs = MruStore::nowMs();
    rebuild();
    applyQuery();
    logUsage("palette.open", {{"rows", visible.size()}});

    // Snapshot where we came from. We deliberately don't peek row 0 here —
    // merely opening the switcher shouldn't move you; the terminal only
    // changes once you start navigating.
    sessions.beginPeek();
    repaint();
}

PaletteItem Palette::makeSessionItem(TermSession& session) const
{
    auto item = PaletteItem {};
    item.kind = PaletteItem::Kind::Session;
    item.session = &session;
    item.key = session.key();
    item.label = session.name;
    item.claude = session.isClaude();
    item.lastUsed = sessions.lastUsed(item.key);

    const auto title = session.activeTitle();
    item.detail = title;

    if (!item.claude || title.empty())
        item.detail = compactHomePath(session.activeWorkingDirectory());

    if (const auto panes = session.view.paneCount(); panes > 1)
        item.detail += "  ·  " + std::to_string(panes) + " panes";

    if (!session.lastNotify.empty())
        item.status = session.lastNotify;

    return item;
}

PaletteItem Palette::makeProjectItem(const std::string& name, const std::string& path) const
{
    auto item = PaletteItem {};
    item.kind = PaletteItem::Kind::Project;
    item.key = path;
    item.label = name;
    item.detail = compactHomePath(path);
    item.lastUsed = sessions.lastUsed(path);
    return item;
}

void Palette::rebuild()
{
    allItems.clear();

    for (auto& session: sessions.all())
        allItems.push_back(makeSessionItem(*session));

    for (auto& project: scanProjects(config))
    {
        if (sessions.find(project.path) == nullptr)
            allItems.push_back(makeProjectItem(project.name, project.path));
    }
}

void Palette::applyQuery()
{
    visible.clear();

    const auto& query = searchField.getText();

    if (query.empty())
    {
        // Open sessions first, then projects, each most-recently-used first.
        visible = allItems;
        std::stable_sort(visible.begin(), visible.end(), isOrderedForEmptyQuery);
    }
    else
    {
        visible = rankPaletteItems(allItems, query, sessions.getPaletteUsage(query));
    }

    selected = std::clamp(selected, 0, std::max((int) visible.size() - 1, 0));
}

void Palette::chooseSelected()
{
    if (selected < (int) visible.size())
    {
        const auto item = visible[(std::size_t) selected];
        sessions.recordPalettePick(searchField.getText(), item.key);
        logPick(item);

        if (isOpenSession(item))
        {
            // The peek already put us here; Enter just commits it as a real
            // switch (recording recency) and dismisses the box.
            sessions.peekTo(*item.session);
            sessions.endPeek(true);
        }
        else
        {
            // A project isn't open yet, so it was never peeked (spawning a
            // shell is a side effect). Drop the peek back to the origin, then
            // actually open it.
            sessions.endPeek(false);
            sessions.openProject(item.key);
        }
    }
    else
    {
        logCancel();
        sessions.endPeek(false);
    }

    shown = false;
    onClosed();
}

void Palette::dismiss()
{
    // Escape / click-away: the peek was only a preview, so put the terminal
    // back where it was.
    logCancel();
    sessions.endPeek(false);
    shown = false;
    onClosed();
}

std::int64_t Palette::measureMsSinceOpen() const
{
    return MruStore::nowMs() - openedAtMs;
}

void Palette::logPick(const PaletteItem& item) const
{
    const auto kind = isOpenSession(item) ? "session" : "project";
    logUsage("palette.pick",
             {{"kind", kind},
              {"name", item.label},
              {"row", selected},
              {"query_length", searchField.getText().size()},
              {"keys", keysSinceOpen},
              {"ms", measureMsSinceOpen()}});
}

void Palette::logCancel() const
{
    logUsage("palette.cancel",
             {{"query_length", searchField.getText().size()},
              {"keys", keysSinceOpen},
              {"ms", measureMsSinceOpen()}});
}

void Palette::peekSelected()
{
    if (selected >= (int) visible.size())
        return;

    const auto& item = visible[(std::size_t) selected];

    // Only open sessions live-peek — switching to one is free. Projects would
    // spawn a shell per highlighted row, so they wait for Enter.
    if (isOpenSession(item))
        sessions.peekTo(*item.session);
}

void Palette::moveSelection(int delta)
{
    if (!visible.empty())
    {
        selected = wrapSelection(selected, delta, (int) visible.size());
        peekSelected();
        repaint();
    }
}

bool Palette::keyDown(const KeyEvent& event)
{
    ++keysSinceOpen;
    const auto wasNormalMode = searchField.isNormalMode();

    if (!handleSearchFieldKey(event))
        handleNavigationKey(event);

    if (shown && searchField.isNormalMode() != wasNormalMode)
        logUsage("palette.mode", {{"mode", searchField.isNormalMode() ? "normal" : "insert"}});

    return true;
}

bool Palette::handleNavigationKey(const KeyEvent& event)
{
    if (event.modifiers.command && event.charactersIgnoringModifiers == "k")
        dismiss();
    else if (isSelectPreviousKey(event))
        moveSelection(-1);
    else if (isSelectNextKey(event))
        moveSelection(1);
    else
        return false;

    return true;
}

void Palette::searchTextChanged()
{
    applyQuery();
    logUsage("palette.query",
             {{"query_length", searchField.getText().size()}, {"rows", visible.size()}});
    peekSelected();
}

int Palette::countRows() const
{
    return (int) visible.size();
}

Rect Palette::calculatePanelBounds() const
{
    return calculateListPanelBounds(getLocalBounds(), listMetrics, (int) visible.size());
}

int Palette::findRowAt(Point pos) const
{
    return findListRowAt(calculatePanelBounds(), listMetrics, (int) visible.size(), pos);
}

void Palette::mouseMove(const MouseEvent& event)
{
    if (const auto row = findRowAt(event.position); row >= 0 && row != selected)
    {
        selected = row;
        peekSelected();
        repaint();
    }
}

void Palette::paintRow(UI::Graphics& g, const PaletteItem& item, Rect rowArea)
{
    const auto baseline = rowArea.y + listMetrics.rowHeight * baselineFraction;
    auto x = rowArea.x + iconLeft;

    g.setColour(toColor(getItemIconColour(item, theme)));
    drawTextInFont(g, getItemIcon(item), {x, baseline}, rowFont);
    x += labelAfterIcon;

    const auto label = truncateWithEllipsis(item.label, labelLength);
    g.setColour(toColor(theme.foreground));
    drawTextInFont(g, label, {x, baseline}, rowFont);
    x += measureTextInFont(g, label, rowFont) + labelToDetailGap;

    g.setColour(toColor(theme.ansi[8]));
    drawTextInFont(g, describeItem(item), {x, baseline}, detailFont);

    if (item.claude)
    {
        const auto badge = std::string {"claude"};
        const auto badgeWidth = measureTextInFont(g, badge, detailFont);
        g.setColour(toColor(theme.ansi[5]));
        drawTextInFont(g,
                       badge,
                       {rowArea.right() - badgeWidth - badgeRightPadding, baseline},
                       detailFont);
    }
}

void Palette::paint(UI::Graphics& g)
{
    const auto panel = calculatePanelBounds();

    paintListBackdropAndCard(g, panel);

    paintSearchField(g, {panel.x + textLeft, panel.y + headerBaseline});

    paintHeaderDivider(g, panel, listMetrics.headerHeight);

    const auto rows = countVisibleListRows(listMetrics, (int) visible.size());

    for (auto i = 0; i < rows; ++i)
    {
        const auto rowArea = Rect {panel.x + rowSideInset,
                                   calculateListRowTop(panel, listMetrics, i),
                                   panel.w - 2.0f * rowSideInset,
                                   listMetrics.rowHeight - rowGap};

        if (i == selected)
            paintSelectedRowHighlight(g, rowArea);

        paintRow(g, visible[(std::size_t) i], rowArea);
    }

    if (visible.empty())
    {
        const auto emptyBaseline =
            panel.y + listMetrics.headerHeight + listMetrics.rowHeight * baselineFraction;
        g.setColour(toColor(theme.ansi[8]));
        drawTextInFont(g, "no matches", {panel.x + textLeft, emptyBaseline}, rowFont);
    }
}
} // namespace term
