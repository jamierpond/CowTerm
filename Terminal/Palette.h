#pragma once

#include "Config.h"
#include "Session.h"

#include <functional>
#include <string>
#include <vector>

namespace term
{
struct PaletteItem
{
    enum class Kind
    {
        Session,
        Project
    };

    Kind kind = Kind::Project;
    std::string key;
    std::string label;
    std::string detail;
    std::string status;
    bool claude = false;
    std::int64_t lastUsed = 0;
    TermSession* session = nullptr;
};

// The switcher: one overlay that fuzzy-searches everything — open sessions
// (Claude sessions surfaced with their conversation title and last notify),
// then known project dirs that aren't open yet. Empty query lists by
// recency, Wim-style; Enter switches or spawns. CPU-painted chrome over the
// GPU terminal.
class Palette final : public eacp::Graphics::View
{
public:
    Palette(const AppConfig& configToUse, SessionManager& sessionsToUse);

    void show();
    bool isShown() const { return shown; }

    // Fired for both dismissal and selection; the shell removes the overlay
    // and restores terminal focus.
    eacp::Callback onClosed = [] {};

    // Ctrl+W on a highlighted repo: create a new git worktree + branch based
    // on it and open a session there. Returns "" on success (the palette then
    // closes), or a git error to display in the branch prompt on failure.
    std::function<std::string(const std::string& repoPath, const std::string& branch)>
        onCreateWorktree =
            [](const std::string&, const std::string&) { return std::string {}; };

    void paint(eacp::Graphics::Context& context) override;
    void keyDown(const eacp::Graphics::KeyEvent& event) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;

private:
    void rebuild();
    void applyQuery();
    void choose();
    void cancel();
    void peekSelected();
    void moveSelection(int delta);
    void popQueryChar();
    int rowAt(eacp::Graphics::Point pos) const;
    eacp::Graphics::Rect panelBounds() const;

    // The worktree branch-name sub-prompt.
    void beginWorktree();
    void worktreeKeyDown(const eacp::Graphics::KeyEvent& event);
    void createWorktreeFromInput();
    void exitWorktree();
    void paintWorktree(eacp::Graphics::Context& context);

    const AppConfig& config;
    SessionManager& sessions;
    Theme theme;

    std::vector<PaletteItem> allItems;
    std::vector<PaletteItem> visible;
    std::string query;
    int selected = 0;
    bool shown = false;

    // Worktree sub-prompt: when active the list is replaced by a single
    // branch-name field bound to the repo we entered from.
    bool worktreeMode = false;
    std::string worktreeRepoPath;
    std::string worktreeRepoName;
    std::string branchName;
    std::string worktreeError;

    eacp::Graphics::Font queryFont;
    eacp::Graphics::Font rowFont;
    eacp::Graphics::Font detailFont;
};
} // namespace term
