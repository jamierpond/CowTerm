#include "Palette.h"

#include "FuzzyMatch.h"
#include "GitWorktree.h"
#include "Projects.h"
#include "TextLayout.h"

#include <algorithm>

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
constexpr auto listMetrics = ListPanelMetrics {.maximumWidth = 680.0f,
                                               .rowHeight = 34.0f,
                                               .headerHeight = 46.0f,
                                               .hintHeight = 0.0f,
                                               .maximumRows = 12,
                                               .topFraction = 0.14f};
constexpr auto fontSizes = OverlayFontSizes {.header = 16.0f, .row = 14.0f, .detail = 12.0f};

constexpr auto subPromptRowCount = 3;
constexpr auto cardBorderAlpha = 0.8f;
constexpr auto textLeft = 18.0f;
constexpr auto headerBaseline = 30.0f;
constexpr auto subPromptTitleGap = 12.0f;
constexpr auto subPromptTargetLength = 40;
constexpr auto subPromptBodyFraction = 0.7f;
constexpr auto subPromptHintFraction = 1.9f;
constexpr auto subPromptErrorLength = 66;
constexpr auto branchFieldLeft = 88.0f;

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

bool isPlainTypedText(const KeyEvent& event)
{
    return !event.modifiers.command && !event.modifiers.control
           && isPrintableTypedText(event.characters);
}

bool isOrderedForEmptyQuery(const PaletteItem& a, const PaletteItem& b)
{
    if (a.kind != b.kind)
        return isSessionItem(a);

    return a.lastUsed > b.lastUsed;
}
} // namespace

Palette::Palette(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : OverlayPanel(configToUse, sessionsToUse, fontSizes)
{
}

void Palette::show()
{
    query.clear();
    selected = 0;
    shown = true;
    worktreeMode = false;
    worktreeError.clear();
    confirmDeleteMode = false;
    deleteError.clear();
    rebuild();
    applyQuery();

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

    if (query.empty())
    {
        // Open sessions first, then projects, each most-recently-used first.
        visible = allItems;
        std::stable_sort(visible.begin(), visible.end(), isOrderedForEmptyQuery);
    }
    else
    {
        auto scored = std::vector<std::pair<int, const PaletteItem*>> {};

        for (const auto& item: allItems)
        {
            const auto haystack = item.label + " " + item.detail + " " + item.status
                                  + (item.claude ? " claude" : "");

            if (auto score = fuzzyScore(query, haystack))
                scored.emplace_back(*score, &item);
        }

        std::stable_sort(scored.begin(),
                         scored.end(),
                         [](const auto& a, const auto& b)
                         {
                             if (a.first != b.first)
                                 return a.first > b.first;

                             return a.second->lastUsed > b.second->lastUsed;
                         });

        for (auto& [score, item]: scored)
            visible.push_back(*item);
    }

    selected = std::clamp(selected, 0, std::max((int) visible.size() - 1, 0));
}

void Palette::chooseSelected()
{
    if (selected < (int) visible.size())
    {
        const auto item = visible[(std::size_t) selected];

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
        sessions.endPeek(false);
    }

    shown = false;
    onClosed();
}

void Palette::dismiss()
{
    // Escape / click-away: the peek was only a preview, so put the terminal
    // back where it was.
    sessions.endPeek(false);
    shown = false;
    onClosed();
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
    if (worktreeMode)
        worktreeKeyDown(event);
    else if (confirmDeleteMode)
        confirmDeleteKeyDown(event);
    else if (!handleNavigationKey(event) && !handleCommandKey(event))
        editQuery(event);

    return true;
}

bool Palette::handleNavigationKey(const KeyEvent& event)
{
    const auto isCloseKey = event.keyCode == KeyCode::Escape
                            || (event.modifiers.command
                                && event.charactersIgnoringModifiers == "k");

    if (isCloseKey)
        dismiss();
    else if (event.keyCode == KeyCode::Return)
        chooseSelected();
    else if (isSelectPreviousKey(event))
        moveSelection(-1);
    else if (isSelectNextKey(event))
        moveSelection(1);
    else
        return false;

    return true;
}

bool Palette::handleCommandKey(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Delete)
        return false;

    const auto& chars = event.charactersIgnoringModifiers;

    // Ctrl+W on the highlighted repo: branch off it into a fresh worktree.
    if (event.modifiers.control && chars == "w")
        beginWorktree();

    // Ctrl+X on a highlighted worktree: move it to the trash (with a confirm).
    else if (event.modifiers.control && chars == "x")
        beginRemoveWorktree();

    return event.modifiers.command || event.modifiers.control;
}

void Palette::editQuery(const KeyEvent& event)
{
    if (!editQueryText(query, event))
        return;

    applyQuery();
    peekSelected();
    repaint();
}

// A repo the user already has open shows up as a Session, so take its project
// dir; an unopened one is a Project whose key is the path.
std::optional<Palette::SelectedRepository> Palette::findSelectedRepository() const
{
    if (selected >= (int) visible.size())
        return std::nullopt;

    const auto& item = visible[(std::size_t) selected];

    if (item.session != nullptr)
        return SelectedRepository {item.session->projectDir, item.label};

    return SelectedRepository {item.key, item.label};
}

void Palette::beginWorktree()
{
    const auto repo = findSelectedRepository();

    if (!repo || repo->path.empty())
        return;

    // Keep the source repo's live peek up while we name the branch -- the
    // background terminal should show the session we're forking, not the one
    // the palette was opened from. Navigation already peeked this row (open
    // sessions only); an unopened project has nothing to preview.
    worktreeMode = true;
    worktreeRepoPath = repo->path;
    worktreeRepoName = repo->label;
    branchName.clear();
    worktreeError.clear();
    repaint();
}

void Palette::worktreeKeyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        exitWorktree();
        return;
    }

    if (event.keyCode == KeyCode::Return)
    {
        createWorktreeFromInput();
        return;
    }

    if (event.keyCode == KeyCode::Delete)
        eraseLastCodepoint(branchName);
    else if (isPlainTypedText(event))
        branchName += event.characters;
    else
        return;

    worktreeError.clear();
    repaint();
}

void Palette::createWorktreeFromInput()
{
    if (branchName.empty())
        return;

    // The shell runs git and, on success, opens the session in the new
    // worktree. A non-empty return is git's error — keep the prompt up so
    // the user can read it and retry the name.
    if (const auto error = onCreateWorktree(worktreeRepoPath, branchName); error.empty())
    {
        // onCreateWorktree opened a session in the new worktree, so it's now
        // the active peek. Commit it as a real switch so it sticks instead of
        // reverting to wherever we opened the palette from.
        sessions.endPeek(true);
        worktreeMode = false;
        shown = false;
        onClosed();
    }
    else
    {
        worktreeError = error;
        repaint();
    }
}

void Palette::exitWorktree()
{
    worktreeMode = false;
    branchName.clear();
    worktreeError.clear();

    // Back to the list. The peek was never torn down (we kept the source
    // visible while naming), and peekOrigin still points at where we opened
    // from, so just re-preview the highlighted row — a fresh beginPeek() here
    // would clobber the real origin with the source repo.
    peekSelected();
    repaint();
}

void Palette::beginRemoveWorktree()
{
    const auto target = findSelectedRepository();

    // Only a genuine linked worktree may be trashed -- git decides, since the
    // "<repo>-<leaf>" layout is otherwise indistinguishable from a plain repo.
    if (!target || !isWorktree(target->path))
        return;

    // Trashing is a side effect, not a switch -- and the target may well be the
    // session we're currently peeking. Drop the preview back to the origin
    // before we take over the panel, so we're not previewing a doomed session.
    sessions.endPeek(false);

    confirmDeleteMode = true;
    deleteTargetPath = target->path;
    deleteTargetName = target->label;
    deleteError.clear();
    repaint();
}

void Palette::confirmDeleteKeyDown(const KeyEvent& event)
{
    // Destructive, so require an explicit "y" — Enter deliberately does
    // nothing here to defeat Ctrl+X-then-reflexive-Enter.
    if (event.keyCode == KeyCode::Escape
        || event.charactersIgnoringModifiers == "n")
    {
        exitConfirmDelete();
        return;
    }

    if (event.charactersIgnoringModifiers == "y")
        performRemoveWorktree();
}

void Palette::performRemoveWorktree()
{
    // onRemoveWorktree trashes the checkout and closes any session on it. A
    // non-empty return is the OS/git error — keep the prompt up so the user
    // can read it.
    if (const auto error = onRemoveWorktree(deleteTargetPath); error.empty())
    {
        confirmDeleteMode = false;
        shown = false;
        onClosed();
    }
    else
    {
        deleteError = error;
        repaint();
    }
}

void Palette::exitConfirmDelete()
{
    confirmDeleteMode = false;
    deleteError.clear();

    // Back to the list. Unlike the worktree prompt, the peek was torn down on
    // entry, so re-arm it fresh for the row we're back on.
    sessions.beginPeek();
    peekSelected();
    repaint();
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
    if (worktreeMode || confirmDeleteMode)
        return;

    if (const auto row = findRowAt(event.position); row >= 0 && row != selected)
    {
        selected = row;
        peekSelected();
        repaint();
    }
}

void Palette::mouseDown(const MouseEvent& event)
{
    // The sub-prompts are keyboard-only; a stray click mustn't tear them down.
    if (!worktreeMode && !confirmDeleteMode)
        OverlayPanel::mouseDown(event);
}

Rect Palette::paintSubPrompt(UI::Graphics& g, const SubPrompt& prompt)
{
    const auto panel =
        calculateListPanelBounds(getLocalBounds(), listMetrics, subPromptRowCount);
    const auto titleBaseline = panel.y + headerBaseline;

    paintListBackdropAndCardWithBorder(g, panel, prompt.borderColour);

    // Header: what we're doing, and which repo it acts on.
    g.setColour(prompt.titleColour);
    drawTextInFont(g, prompt.title, {panel.x + textLeft, titleBaseline}, headerFont);

    const auto titleWidth = measureTextInFont(g, prompt.title, headerFont);
    g.setColour(toColor(theme.ansi[8]));
    drawTextInFont(g,
                   "·  " + truncateWithEllipsis(prompt.targetName, subPromptTargetLength),
                   {panel.x + textLeft + titleWidth + subPromptTitleGap, titleBaseline},
                   headerFont);

    paintHeaderDivider(g, panel, listMetrics.headerHeight);

    // Hint, or the complaint if the last attempt failed.
    const auto hintPosition =
        Point {panel.x + textLeft,
               panel.y + listMetrics.headerHeight + listMetrics.rowHeight * subPromptHintFraction};

    if (!prompt.error.empty())
    {
        g.setColour(toColor(theme.ansi[1]));
        drawTextInFont(g,
                       truncateWithEllipsis(prompt.error, subPromptErrorLength),
                       hintPosition,
                       detailFont);
    }
    else
    {
        g.setColour(toColor(theme.ansi[8]));
        drawTextInFont(g, prompt.hint, hintPosition, detailFont);
    }

    return panel;
}

void Palette::paintWorktree(UI::Graphics& g)
{
    const auto panel = paintSubPrompt(g,
                                      {.title = "new worktree",
                                       .targetName = worktreeRepoName,
                                       .error = worktreeError,
                                       .hint = "Enter: create worktree · Esc: back",
                                       .titleColour = toColor(theme.ansi[5]),
                                       .borderColour = toColor(theme.selection, cardBorderAlpha)});

    const auto branchY =
        panel.y + listMetrics.headerHeight + listMetrics.rowHeight * subPromptBodyFraction;
    g.setColour(toColor(theme.ansi[8]));
    drawTextInFont(g, "branch", {panel.x + textLeft, branchY}, detailFont);
    g.setColour(toColor(theme.foreground));
    drawTextInFont(g, "› " + branchName + "▏", {panel.x + branchFieldLeft, branchY}, rowFont);
}

void Palette::paintConfirmDelete(UI::Graphics& g)
{
    // Red-tinted border flags this as the destructive branch.
    const auto panel = paintSubPrompt(g,
                                      {.title = "trash worktree",
                                       .targetName = deleteTargetName,
                                       .error = deleteError,
                                       .hint = "y: move to Trash · n: cancel",
                                       .titleColour = toColor(theme.ansi[1]),
                                       .borderColour = toColor(theme.ansi[1], cardBorderAlpha)});

    const auto questionY =
        panel.y + listMetrics.headerHeight + listMetrics.rowHeight * subPromptBodyFraction;
    g.setColour(toColor(theme.foreground));
    drawTextInFont(
        g, "Move this worktree to the Trash?", {panel.x + textLeft, questionY}, rowFont);
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
    if (worktreeMode)
    {
        paintWorktree(g);
        return;
    }

    if (confirmDeleteMode)
    {
        paintConfirmDelete(g);
        return;
    }

    const auto panel = calculatePanelBounds();

    paintListBackdropAndCard(g, panel);

    g.setColour(toColor(theme.foreground));
    drawTextInFont(
        g, "› " + query + "▏", {panel.x + textLeft, panel.y + headerBaseline}, headerFont);

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
