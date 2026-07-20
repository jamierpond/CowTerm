#pragma once

#include "Config.h"
#include "MruStore.h"
#include "SessionView.h"

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <memory>
#include <string>
#include <vector>

namespace term
{
struct SavedSession
{
    std::string name;
    std::string projectDir;
    std::string cwd;
    std::vector<SavedPane> panes;

    MIRO_REFLECT(name, projectDir, cwd, panes)
};

struct SavedState
{
    std::vector<SavedSession> sessions;
    int activeIndex = 0;

    MIRO_REFLECT(sessions, activeIndex)
};

// One named pane tree bound to a project directory.
class TermSession
{
public:
    TermSession(const AppConfig& config,
                std::string nameToUse,
                std::string projectDirToUse,
                const std::string& startCwd);

    // Stable identity for MRU stamps and notification routing.
    const std::string& key() const { return projectDir.empty() ? name : projectDir; }

    // True when a Claude Code conversation owns any pane of this session.
    bool isClaude() const { return view.isClaudeAnywhere(); }

    // The active pane's title / directory; empty when the session has no
    // panes (mid-teardown).
    std::string activeTitle() const;
    std::string activeWorkingDirectory() const;

    std::string name;
    std::string projectDir;
    std::string lastNotify;

    // When lastNotify arrived, and whether it landed while the session was
    // in the background and hasn't been looked at since — the Claude HUD's
    // "waiting on you" signal. Cleared the moment the session is shown.
    std::int64_t lastNotifyAt = 0;
    bool notifyUnseen = false;

    SessionView view;
};

// Owns every open session plus the recency store, and persists the open set
// (emberstore) so a relaunch brings the workspace back: shells respawn in
// their last cwd. Running processes don't survive a quit — that's the
// daemon's job, later.
class SessionManager
{
public:
    explicit SessionManager(const AppConfig& configToUse);
    ~SessionManager();

    TermSession* active() { return activeSession; }
    const std::vector<std::unique_ptr<TermSession>>& all() const { return sessions; }

    TermSession* find(const std::string& key);
    TermSession& openProject(const std::string& dir);

    // Always creates a fresh session (openProject dedupes by directory).
    TermSession& newSession(const std::string& dir);
    void switchTo(TermSession& session);
    void switchToIndex(int index);
    void switchToLast();

    // Live-peek support for the palette. The palette shows the terminal
    // underneath switching to whatever row is highlighted, so navigating the
    // list is itself the navigation and Enter merely dismisses. beginPeek
    // snapshots the active session; peekTo previews one (swaps active +
    // notifies, but records no recency and never persists — a peek isn't a
    // "use"); endPeek(true) commits the last peek as a real switch, endPeek(
    // false) restores the pre-peek session.
    void beginPeek();
    void peekTo(TermSession& session);
    void endPeek(bool commit);
    void close(TermSession& session);
    void restoreOrCreateInitial();

    std::int64_t lastUsed(const std::string& key) { return mru.lastUsed(key); }

    std::function<void(TermSession&)> onActiveChanged = [](TermSession&) {};
    std::function<void(TermSession&, const std::string&)> onNotify =
        [](TermSession&, const std::string&) {};
    eacp::Callback onSessionsChanged = [] {};
    eacp::Callback onAllClosed = [] {};

    // Fired for every pane the session creates, so the app shell installs
    // per-pane hooks (interceptKey, title tracking).
    std::function<void(TermSession&, TerminalView&)> onPaneWired =
        [](TermSession&, TerminalView&) {};

    void persistNow();

private:
    TermSession& createSession(const std::string& name,
                               const std::string& projectDir,
                               const std::string& startCwd,
                               const std::vector<SavedPane>& panes);
    void wireSession(TermSession& session);
    void closeIfPresent(TermSession* session);
    std::string uniqueName(const std::string& base) const;

    // Marks the state dirty and schedules a single deferred write, so hot
    // paths (pane focus, cwd changes) never pay for disk IO. writeState is
    // the write itself.
    void persist();
    void writeState();

    const AppConfig& config;
    emberstore::Database db;
    MruStore mru;
    emberstore::Document<SavedState> saved;
    std::vector<std::unique_ptr<TermSession>> sessions;
    TermSession* activeSession = nullptr;
    TermSession* previousSession = nullptr;
    TermSession* peekOrigin = nullptr;
    bool peeking = false;
    bool persistPending = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace term
