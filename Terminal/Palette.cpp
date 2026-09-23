#include "Palette.h"

#include "FuzzyMatch.h"
#include "GitWorktree.h"
#include "Projects.h"


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
constexpr float panelWidth = 680.0f;
constexpr float rowHeight = 34.0f;
constexpr float headerHeight = 46.0f;
constexpr int maxRows = 12;

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

} // namespace

Palette::Palette(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : config(configToUse)
    , sessions(sessionsToUse)
    , theme(themeByName(configToUse.theme))
    , queryFont({config.font, 16.0f})
    , rowFont({config.font, 14.0f})
    , detailFont({config.font, 12.0f})
{
    setInterceptsMouseClicks(true);
    setWantsKeyboardFocus(true);
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

void Palette::rebuild()
{
    allItems.clear();

    for (auto& session: sessions.all())
    {
        auto item = PaletteItem {};
        item.kind = PaletteItem::Kind::Session;
        item.session = session.get();
        item.key = session->key();
        item.label = session->name;
        item.claude = session->isClaude();
        item.lastUsed = sessions.lastUsed(item.key);

        const auto title = session->activeTitle();
        item.detail = item.claude && !title.empty()
                          ? title
                          : compactPath(session->activeWorkingDirectory());

        if (const auto panes = session->view.paneCount(); panes > 1)
            item.detail += "  ·  " + std::to_string(panes) + " panes";

        if (!session->lastNotify.empty())
            item.status = session->lastNotify;

        allItems.push_back(std::move(item));
    }

    for (auto& project: scanProjects(config))
    {
        if (sessions.find(project.path) != nullptr)
            continue;

        auto item = PaletteItem {};
        item.kind = PaletteItem::Kind::Project;
        item.key = project.path;
        item.label = project.name;
        item.detail = compactPath(project.path);
        item.lastUsed = sessions.lastUsed(project.path);
        allItems.push_back(std::move(item));
    }
}

void Palette::applyQuery()
{
    visible.clear();

    if (query.empty())
    {
        // Open sessions first, then projects, each most-recently-used first.
        visible = allItems;
        std::stable_sort(visible.begin(),
                         visible.end(),
                         [](const auto& a, const auto& b)
                         {
                             if (a.kind != b.kind)
                                 return a.kind == PaletteItem::Kind::Session;

                             return a.lastUsed > b.lastUsed;
                         });
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

void Palette::choose()
{
    if (selected < (int) visible.size())
    {
        const auto item = visible[(std::size_t) selected];

        if (item.kind == PaletteItem::Kind::Session && item.session != nullptr)
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

void Palette::cancel()
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
    if (item.kind == PaletteItem::Kind::Session && item.session != nullptr)
        sessions.peekTo(*item.session);
}

void Palette::moveSelection(int delta)
{
    if (!visible.empty())
    {
        selected = (selected + delta + (int) visible.size()) % (int) visible.size();
        peekSelected();
        repaint();
    }
}

void Palette::popQueryChar()
{
    while (!query.empty())
    {
        const auto last = (unsigned char) query.back();
        query.pop_back();

        if ((last & 0xc0) != 0x80)
            break;
    }
}

bool Palette::keyDown(const KeyEvent& event)
{
    if (worktreeMode)
    {
        worktreeKeyDown(event);
        return true;
    }

    if (confirmDeleteMode)
    {
        confirmDeleteKeyDown(event);
        return true;
    }

    if (event.keyCode == KeyCode::Escape
        || (event.modifiers.command && event.charactersIgnoringModifiers == "k"))
    {
        cancel();
        return true;
    }

    if (event.keyCode == KeyCode::Return)
    {
        choose();
        return true;
    }

    if (event.keyCode == KeyCode::UpArrow
        || (event.modifiers.control && event.charactersIgnoringModifiers == "p"))
    {
        moveSelection(-1);
        return true;
    }

    if (event.keyCode == KeyCode::DownArrow
        || (event.modifiers.control && event.charactersIgnoringModifiers == "n"))
    {
        moveSelection(1);
        return true;
    }

    if (event.keyCode == KeyCode::Delete)
    {
        popQueryChar();
        applyQuery();
        peekSelected();
        repaint();
        return true;
    }

    // Ctrl+W on the highlighted repo: branch off it into a fresh worktree.
    if (event.modifiers.control && event.charactersIgnoringModifiers == "w")
    {
        beginWorktree();
        return true;
    }

    // Ctrl+X on a highlighted worktree: move it to the trash (with a confirm).
    if (event.modifiers.control && event.charactersIgnoringModifiers == "x")
    {
        beginRemoveWorktree();
        return true;
    }

    if (event.modifiers.command || event.modifiers.control)
        return true;

    const auto& text = event.characters;

    if (!text.empty() && (unsigned char) text[0] >= 0x20 && text[0] != 0x7f)
    {
        query += text;
        selected = 0;
        applyQuery();
        peekSelected();
        repaint();
    }

    return true;
}

void Palette::beginWorktree()
{
    if (selected >= (int) visible.size())
        return;

    const auto& item = visible[(std::size_t) selected];

    // A repo the user already has open shows up as a Session, so take its
    // project dir; an unopened one is a Project whose key is the path.
    const auto repo =
        item.session != nullptr ? item.session->projectDir : item.key;

    if (repo.empty())
        return;

    // Keep the source repo's live peek up while we name the branch — the
    // background terminal should show the session we're forking, not the one
    // the palette was opened from. Navigation already peeked this row (open
    // sessions only); an unopened project has nothing to preview.
    worktreeMode = true;
    worktreeRepoPath = repo;
    worktreeRepoName = item.label;
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
    {
        while (!branchName.empty())
        {
            const auto last = (unsigned char) branchName.back();
            branchName.pop_back();

            if ((last & 0xc0) != 0x80)
                break;
        }

        worktreeError.clear();
        repaint();
        return;
    }

    if (event.modifiers.command || event.modifiers.control)
        return;

    const auto& text = event.characters;

    if (!text.empty() && (unsigned char) text[0] >= 0x20 && text[0] != 0x7f)
    {
        branchName += text;
        worktreeError.clear();
        repaint();
    }
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
    if (selected >= (int) visible.size())
        return;

    const auto& item = visible[(std::size_t) selected];

    // An open repo is a Session (take its project dir); an unopened one is a
    // Project keyed by path. Either way, only worktree checkouts can be binned.
    const auto path =
        item.session != nullptr ? item.session->projectDir : item.key;

    // Only a genuine linked worktree may be trashed — git decides, since the
    // "<repo>-<leaf>" layout is otherwise indistinguishable from a plain repo.
    if (!isWorktree(path))
        return;

    // Trashing is a side effect, not a switch — and the target may well be the
    // session we're currently peeking. Drop the preview back to the origin
    // before we take over the panel, so we're not previewing a doomed session.
    sessions.endPeek(false);

    confirmDeleteMode = true;
    deleteTargetPath = path;
    deleteTargetName = item.label;
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

Rect Palette::panelBounds() const
{
    const auto bounds = getLocalBounds();
    const auto width = std::min(panelWidth, bounds.w - 60.0f);
    const auto rows = std::min((int) visible.size(), maxRows);
    const auto height = headerHeight + (float) std::max(rows, 1) * rowHeight + 12.0f;

    return {
        (bounds.w - width) / 2.0f, std::max(bounds.h * 0.14f, 20.0f), width, height};
}

int Palette::rowAt(Point pos) const
{
    const auto panel = panelBounds();
    const auto y = pos.y - (panel.y + headerHeight + 4.0f);

    if (pos.x < panel.x || pos.x > panel.right() || y < 0)
        return -1;

    const auto row = (int) (y / rowHeight);
    return row < std::min((int) visible.size(), maxRows) ? row : -1;
}

void Palette::mouseMove(const MouseEvent& event)
{
    if (worktreeMode || confirmDeleteMode)
        return;

    if (const auto row = rowAt(event.position); row >= 0 && row != selected)
    {
        selected = row;
        peekSelected();
        repaint();
    }
}

void Palette::mouseDown(const MouseEvent& event)
{
    // The sub-prompts are keyboard-only; a stray click mustn't tear them down.
    if (worktreeMode || confirmDeleteMode)
        return;

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

void Palette::paintWorktree(UI::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const auto width = std::min(panelWidth, bounds.w - 60.0f);
    const auto height = headerHeight + rowHeight * 3.0f + 12.0f;
    const auto panel = Rect {
        (bounds.w - width) / 2.0f, std::max(bounds.h * 0.14f, 20.0f), width, height};

    g.setColour(Color::black(0.38f));
    g.fillRect(bounds);

    g.setColour(toColor(theme.background).brighter(0.04f));
    g.fillRoundedRect(panel, 12.0f);

    g.setColour(toColor(theme.selection, 0.8f));
    g.drawRect(panel, 1.0f);

    // Header: what we're doing, and which repo it branches from.
    g.setColour(toColor(theme.ansi[5]));
    drawText(g, "new worktree", {panel.x + 18.0f, panel.y + 30.0f}, queryFont);

    const auto headWidth =
        measureWidth(g, "new worktree", queryFont);
    g.setColour(toColor(theme.ansi[8]));
    drawText(g, "·  " + truncated(worktreeRepoName, 40),
                     {panel.x + 18.0f + headWidth + 12.0f, panel.y + 30.0f},
                     queryFont);

    g.setColour(toColor(theme.selection));
    g.drawLine({panel.x + 12.0f, panel.y + headerHeight - 2.0f},
                     {panel.right() - 12.0f, panel.y + headerHeight - 2.0f});

    // Branch-name field.
    const auto branchY = panel.y + headerHeight + rowHeight * 0.7f;
    g.setColour(toColor(theme.ansi[8]));
    drawText(g, "branch", {panel.x + 18.0f, branchY}, detailFont);
    g.setColour(toColor(theme.foreground));
    drawText(g, "› " + branchName + "▏", {panel.x + 88.0f, branchY}, rowFont);

    // Hint, or git's complaint if the last attempt failed.
    const auto hintY = panel.y + headerHeight + rowHeight * 1.9f;

    if (!worktreeError.empty())
    {
        g.setColour(toColor(theme.ansi[1]));
        drawText(g, truncated(worktreeError, 66), {panel.x + 18.0f, hintY}, detailFont);
    }
    else
    {
        g.setColour(toColor(theme.ansi[8]));
        drawText(g, "Enter: create worktree · Esc: back",
                         {panel.x + 18.0f, hintY},
                         detailFont);
    }
}

void Palette::paintConfirmDelete(UI::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const auto width = std::min(panelWidth, bounds.w - 60.0f);
    const auto height = headerHeight + rowHeight * 3.0f + 12.0f;
    const auto panel = Rect {
        (bounds.w - width) / 2.0f, std::max(bounds.h * 0.14f, 20.0f), width, height};

    g.setColour(Color::black(0.38f));
    g.fillRect(bounds);

    g.setColour(toColor(theme.background).brighter(0.04f));
    g.fillRoundedRect(panel, 12.0f);

    // Red-tinted border flags this as the destructive branch.
    g.setColour(toColor(theme.ansi[1], 0.8f));
    g.drawRect(panel, 1.0f);

    // Header: what we're doing, and which worktree it targets.
    g.setColour(toColor(theme.ansi[1]));
    drawText(g, "trash worktree", {panel.x + 18.0f, panel.y + 30.0f}, queryFont);

    const auto headWidth =
        measureWidth(g, "trash worktree", queryFont);
    g.setColour(toColor(theme.ansi[8]));
    drawText(g, "·  " + truncated(deleteTargetName, 40),
                     {panel.x + 18.0f + headWidth + 12.0f, panel.y + 30.0f},
                     queryFont);

    g.setColour(toColor(theme.selection));
    g.drawLine({panel.x + 12.0f, panel.y + headerHeight - 2.0f},
                     {panel.right() - 12.0f, panel.y + headerHeight - 2.0f});

    // The question.
    const auto questionY = panel.y + headerHeight + rowHeight * 0.7f;
    g.setColour(toColor(theme.foreground));
    drawText(g, "Move this worktree to the Trash?",
                     {panel.x + 18.0f, questionY},
                     rowFont);

    // Hint, or the OS/git complaint if the last attempt failed.
    const auto hintY = panel.y + headerHeight + rowHeight * 1.9f;

    if (!deleteError.empty())
    {
        g.setColour(toColor(theme.ansi[1]));
        drawText(g, truncated(deleteError, 66), {panel.x + 18.0f, hintY}, detailFont);
    }
    else
    {
        g.setColour(toColor(theme.ansi[8]));
        drawText(g, "y: move to Trash · n: cancel",
                         {panel.x + 18.0f, hintY},
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

    const auto panel = panelBounds();

    g.setColour(Color::black(0.38f));
    g.fillRect(getLocalBounds());

    g.setColour(toColor(theme.background).brighter(0.04f));
    g.fillRoundedRect(panel, 12.0f);

    g.setColour(toColor(theme.selection, 0.8f));
    g.drawRect(panel, 1.0f);

    // Query line
    const auto queryText = "› " + query + "▏";
    g.setColour(toColor(theme.foreground));
    drawText(g, queryText, {panel.x + 18.0f, panel.y + 30.0f}, queryFont);

    g.setColour(toColor(theme.selection));
    g.drawLine({panel.x + 12.0f, panel.y + headerHeight - 2.0f},
                     {panel.right() - 12.0f, panel.y + headerHeight - 2.0f});

    // Rows
    const auto rows = std::min((int) visible.size(), maxRows);

    for (auto i = 0; i < rows; ++i)
    {
        const auto& item = visible[(std::size_t) i];
        const auto y = panel.y + headerHeight + 4.0f + (float) i * rowHeight;
        const auto rowRect =
            Rect {panel.x + 8.0f, y, panel.w - 16.0f, rowHeight - 2.0f};

        if (i == selected)
        {
            g.setColour(toColor(theme.selection, 0.85f));
            g.fillRoundedRect(rowRect, 6.0f);
        }

        const auto baseline = y + rowHeight * 0.62f;
        auto x = rowRect.x + 12.0f;

        const auto open = item.kind == PaletteItem::Kind::Session;
        const auto icon = item.claude ? "✳" : (open ? "●" : "▸");
        const auto iconColor =
            item.claude ? toColor(theme.ansi[5])
                        : (open ? toColor(theme.ansi[2]) : toColor(theme.ansi[8]));
        g.setColour(iconColor);
        drawText(g, icon, {x, baseline}, rowFont);
        x += 24.0f;

        g.setColour(toColor(theme.foreground));
        drawText(g, truncated(item.label, 32), {x, baseline}, rowFont);
        x += measureWidth(g, truncated(item.label, 32), rowFont)
             + 14.0f;

        const auto detail = !item.status.empty() ? truncated(item.status, 48)
                                                 : truncated(item.detail, 48);
        g.setColour(toColor(theme.ansi[8]));
        drawText(g, detail, {x, baseline}, detailFont);

        if (item.claude)
        {
            const auto badge = std::string {"claude"};
            const auto badgeWidth =
                measureWidth(g, badge, detailFont);
            g.setColour(toColor(theme.ansi[5]));
            drawText(g, 
                badge, {rowRect.right() - badgeWidth - 12.0f, baseline}, detailFont);
        }
    }

    if (visible.empty())
    {
        g.setColour(toColor(theme.ansi[8]));
        drawText(g, 
            "no matches",
            {panel.x + 18.0f, panel.y + headerHeight + rowHeight * 0.62f},
            rowFont);
    }
}
} // namespace term
