#pragma once

#include "Config.h"
#include "Session.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace term
{
struct PrItem
{
    std::string ci;        // statusCheckRollup state (SUCCESS/FAILURE/...), "NONE" when no checks
    std::string pushedAt;  // ISO 8601 UTC of the last push — the list's sort key
    std::string repo;      // owner/name
    std::string number;
    bool draft = false;
    std::string reviewDecision;  // APPROVED / CHANGES_REQUESTED / ""
    std::string reviewers;       // requested reviewers, "@a @b"
    std::string branch;          // head ref
    std::string url;
    std::string title;

    // Open session on a checkout of this PR's repo, resolved against the
    // session manager ("" when none) — the jump-back-to-the-code column.
    std::string sessionKey;
    std::string sessionName;
};

// The PR dashboard (Ctrl+A p): every open PR you authored across all repos,
// newest push first — CI state, age, review state, and which open session has
// the repo checked out. The native port of tmux-pr-dashboard.
//
//   Enter   land in a session on this PR's branch, one worktree per PR:
//           switch if the branch is already checked out somewhere, else add a
//           sibling worktree (cloning the repo first if it isn't on disk).
//   Ctrl+O  open the PR in the browser        Ctrl+Y  copy its URL
//   Ctrl+S  jump to the repo's session as-is  Ctrl+R  refresh
//
// gh runs on a worker thread and the last fetch is cached on disk, so the
// panel opens instantly with slightly-stale rows while fresh ones stream in.
// Typing filters the list fuzzily, like the palette.
class PrDashboard final : public eacp::Graphics::View
{
public:
    PrDashboard(const AppConfig& configToUse, SessionManager& sessionsToUse);
    ~PrDashboard() override;

    void show();
    bool isShown() const { return shown; }

    eacp::Callback onClosed = [] {};

    void paint(eacp::Graphics::Context& context) override;
    void keyDown(const eacp::Graphics::KeyEvent& event) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;

private:
    void refresh();
    void buildSessionMap();
    void applySessionMap();
    void applyQuery();
    void loadCache();
    void choose();
    void cancel();
    void openWorktree(const PrItem& item);
    void switchToSession(const PrItem& item);
    void openInBrowser(const PrItem& item);
    void copyUrl(const PrItem& item);
    void moveSelection(int delta);
    void popQueryChar();
    const PrItem* selectedItem() const;
    int rowAt(eacp::Graphics::Point pos) const;
    eacp::Graphics::Rect panelBounds() const;

    const AppConfig& config;
    SessionManager& sessions;
    Theme theme;

    std::vector<PrItem> allItems;
    std::vector<PrItem> visible;

    // repo slug (lowercased owner/name) -> {session key, session name},
    // derived from each open session's git remote on a worker thread.
    std::map<std::string, std::pair<std::string, std::string>> sessionsByRepo;

    std::string query;
    int selected = 0;
    bool shown = false;
    bool refreshing = false;
    std::string status;  // header-right state: row count, "refreshing…", or the error
    std::string busy;    // non-empty while an Enter jump (clone/worktree) runs

    // Stamped into each Enter jump; bumped on show() so a job orphaned by
    // Esc-then-reopen can't land its session switch out of nowhere.
    int jumpGeneration = 0;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    eacp::Graphics::Font queryFont;
    eacp::Graphics::Font rowFont;
    eacp::Graphics::Font detailFont;
};
} // namespace term
