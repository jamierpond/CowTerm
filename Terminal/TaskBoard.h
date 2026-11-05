#pragma once

#include "AgentBoard.h"
#include "BoardEnvironment.h"
#include "BoardProjects.h"
#include "PullRequests.h"

#include <map>
#include "BoardStore.h"
#include "Config.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/UI/UI.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace term
{
// The task board (Ctrl+A b): every claude running in a CowTerm pane, grouped
// into tasks by directory and git branch, in four columns: needs you, working,
// review, done. j/k and h/l move, Enter focuses the card's pane and closes the
// board. While shown it asks claude for its agents every couple of seconds.
class TaskBoard final : public eacp::UI::Component
{
public:
    // `n`: pick a project, name the task, then the worktree is made.
    enum class Mode
    {
        board,
        pickProject,
        nameTask,
        startingAgent,
        // `p`: Jamie's open pull requests, each with the session it came from.
        pullRequests
    };

    explicit TaskBoard(const AppConfig& configToUse);

    // Where claude and its transcripts are; read from the environment in Main.cpp.
    void setClaudeQuery(const BoardQuery& query);

    // Where moved cards' zones are kept; shared with `ct board move`.
    void setStoreDirectory(const eacp::FilePath& directory);

    // Where `n` clones and branches; read from the environment in Main.cpp.
    void setStartAgentSettings(const StartAgentSettings& settings);

    void show();
    bool isShown() const { return shown; }
    bool isNamingTask() const { return mode == Mode::nameTask; }

    // Shows a snapshot as it is, with no refreshing (offscreen renders).
    void showSnapshot(const BoardSnapshot& snapshotToShow);
    void selectCard(const std::string& key);

    // Opens the pull request view on these, as of this window (offscreen renders).
    void showPullRequests(PullRequestWindow window, const std::vector<LinkedPullRequest>& pullRequests);
    bool isShowingPullRequests() const { return mode == Mode::pullRequests; }

    // Opens the `n` picker on these projects, filtered by the query (offscreen renders).
    void showProjectPicker(const std::vector<Project>& projectsToShow, const std::string& query);

    // What is under a point, from the last paint.
    struct CardAtPoint
    {
        Zone zone;
        std::string key;
    };

    std::optional<CardAtPoint> findCardAt(eacp::UI::Point point) const;
    std::optional<Zone> findColumnAt(eacp::UI::Point point) const;
    std::optional<std::size_t> findPickerRowAt(eacp::UI::Point point) const;
    std::optional<eacp::UI::Rect> findCardBounds(const std::string& key) const;
    std::optional<eacp::UI::Rect> findColumnBounds(Zone zone) const;

    // The column a dragged card is over, highlighted; empty when not dragging.
    std::optional<Zone> getDragTargetZone() const { return dragTargetZone; }

    eacp::Callback onClosed = [] {};

    // CowTerm's panes right now, for matching claude processes to them.
    std::function<std::vector<BoardPane>()> getPanes = [] { return std::vector<BoardPane> {}; };

    // Types the text into the pane, then presses Enter.
    std::function<void(const std::string& shellId, const std::string& text)> onTellPane =
        [](const std::string&, const std::string&) {};

    // A worktree is ready: open a session there running claude.
    std::function<void(const std::string& worktreePath)> onStartAgent = [](const std::string&) {};

    // The linked session has ended: open a pane in its directory running
    // `claude --resume <sessionId>`.
    std::function<void(const std::string& directory, const std::string& sessionId)> onResumeSession =
        [](const std::string&, const std::string&) {};

    std::function<void(const std::string& url)> onOpenUrl = [](const std::string&) {};

    // Switches to the session holding this pane and focuses it.
    std::function<void(const std::string& shellId)> onShowPane = [](const std::string&) {};

    void paint(eacp::UI::Graphics& g) override;
    bool keyDown(const eacp::UI::KeyEvent& event) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;
    void mouseDrag(const eacp::UI::MouseEvent& event) override;
    void mouseUp(const eacp::UI::MouseEvent& event) override;
    void mouseDoubleClick(const eacp::UI::MouseEvent& event) override;

private:

    void beginPickingProject();
    void loadProjects();
    void applyPickerQuery();
    bool pickerKeyDown(const eacp::UI::KeyEvent& event);
    bool nameTaskKeyDown(const eacp::UI::KeyEvent& event);
    void startAgentInWorktree();
    void paintPicker(eacp::UI::Graphics& g);

    struct PullRequestCache
    {
        std::vector<LinkedPullRequest> pullRequests;
        std::string problem;
        std::time_t fetchedAt = 0;
    };

    void beginShowingPullRequests();
    void requestPullRequests();
    std::optional<PullRequestCache> getShownPullRequests() const;
    bool pullRequestKeyDown(const eacp::UI::KeyEvent& event);
    void openSelectedPullRequestSession();
    void openSelectedPullRequestInBrowser();
    void paintPullRequests(eacp::UI::Graphics& g);
    std::string describeLinkedSession(const LinkedPullRequest& linked) const;
    bool pullRequestMouseDown(const eacp::UI::MouseEvent& event);

    void requestRefresh();
    void applySnapshot(BoardSnapshot newSnapshot);
    struct PlacedCard
    {
        std::size_t column = 0;
        std::size_t row = 0;
        eacp::UI::Rect bounds;
    };

    struct PlacedColumn
    {
        std::size_t column = 0;
        eacp::UI::Rect bounds;
    };

    void selectCardAt(std::size_t column, std::size_t row);
    void chooseSelectedProject();
    void focusSelectedCard();
    void moveSelectedCard(Zone zone);
    void close();
    void moveSelectionWithinColumn(int delta);
    void moveSelectionAcrossColumns(int delta);
    void clampSelection();
    std::optional<TaskCard> getSelectedCard() const;
    bool isNarrowLayout() const;

    void paintHeader(eacp::UI::Graphics& g, eacp::UI::Rect area);
    void paintFooter(eacp::UI::Graphics& g, eacp::UI::Rect area);
    void paintColumns(eacp::UI::Graphics& g, eacp::UI::Rect area);
    void paintStacked(eacp::UI::Graphics& g, eacp::UI::Rect area);
    float paintCard(eacp::UI::Graphics& g,
                    const TaskCard& card,
                    eacp::UI::Rect area,
                    bool isSelected,
                    bool isDrawn);
    eacp::UI::Color getZoneColour(Zone zone) const;

    const AppConfig& config;
    Theme theme;
    std::optional<BoardQuery> claudeQuery;

    BoardSnapshot snapshot;
    std::vector<std::vector<TaskCard>> columns;
    std::size_t selectedColumn = 0;
    std::size_t selectedRow = 0;
    std::string selectedKey;
    bool shown = false;
    bool isRefreshing = false;
    bool hasLoaded = false;
    bool isLeaderArmed = false;
    bool isMoveArmed = false;

    std::vector<PlacedCard> placedCards;
    std::vector<PlacedColumn> placedColumns;
    std::vector<eacp::UI::Rect> placedPickerRows;
    std::optional<std::string> pressedCardKey;
    bool isPressOnSelectedCard = false;
    bool isDragging = false;
    std::optional<Zone> dragTargetZone;

    Mode mode = Mode::board;
    std::optional<StartAgentSettings> startAgentSettings;
    std::vector<Project> projects;
    std::vector<Project> shownProjects;
    std::size_t selectedProject = 0;
    std::string pickerQuery;
    std::optional<Project> chosenProject;
    std::string taskTitle;
    std::string startProgress;
    std::string startProblem;
    bool hasRequestedProjects = false;

    PullRequestWindow pullRequestWindow = PullRequestWindow::today;
    std::map<PullRequestWindow, PullRequestCache> pullRequestCaches;
    bool isFetchingPullRequests = false;
    std::size_t selectedPullRequest = 0;
    std::vector<eacp::UI::Rect> placedPullRequestRows;

    std::optional<emberstore::Database> boardDatabase;
    std::optional<emberstore::Document<BoardDocument>> boardDocument;

    std::unique_ptr<eacp::Threads::Timer> refreshTimer;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    eacp::UI::Font headerFont;
    eacp::UI::Font titleFont;
    eacp::UI::Font detailFont;
};
} // namespace term
