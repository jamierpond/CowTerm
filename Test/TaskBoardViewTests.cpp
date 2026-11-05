#include "BoardStore.h"
#include "Config.h"
#include "TaskBoard.h"

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <random>

// The board's mouse: hit-testing from the last paint, click to select, click
// again to go to the pane, drag to move, picker rows. Driven through the
// component's own handlers on an offscreen render. Self-skips without a GPU
// device, where renderToImage hands back an empty image.

using namespace nano;
using namespace eacp;
using namespace term;

namespace
{
constexpr auto boardWidth = 1400.f;
constexpr auto boardHeight = 860.f;

TaskCard makeCard(const std::string& key, Zone zone, SessionActivity activity)
{
    auto card = TaskCard {};
    card.key = key;
    card.taskName = key;
    card.description = "doing " + key;
    card.paneShellId = "pane-" + key;
    card.zone = zone;
    card.activity = activity;
    return card;
}

struct RenderedBoard
{
    RenderedBoard()
    {
        auto random = std::random_device {};
        storeDirectory = std::filesystem::temp_directory_path()
                         / ("cowterm-board-view-tests-" + std::to_string(random()));
        board.setStoreDirectory(FilePath {storeDirectory.string()});
        host.setRootComponent(board);
        host.setBounds({0.f, 0.f, boardWidth, boardHeight});
        host.resized();

        auto snapshot = BoardSnapshot {};
        snapshot.cards = {makeCard("asking", Zone::needsYou, SessionActivity::needsYou),
                          makeCard("busy", Zone::working, SessionActivity::busy),
                          makeCard("other", Zone::working, SessionActivity::idle)};
        board.showSnapshot(snapshot);
        board.onShowPane = [this](const std::string& shellId) { shownPanes.push_back(shellId); };
        board.onTellPane = [this](const std::string& shellId, const std::string& text)
        { toldPanes.push_back(shellId + ": " + text); };
    }

    ~RenderedBoard() { std::filesystem::remove_all(storeDirectory); }

    bool render() { return host.renderToImage(1.f).isValid(); }

    UI::Point centreOf(UI::Rect bounds) const
    {
        return {bounds.x + bounds.w / 2.0f, bounds.y + bounds.h / 2.0f};
    }

    UI::MouseEvent pressAt(UI::Point point) const
    {
        auto event = UI::MouseEvent {};
        event.position = point;
        event.downPosition = point;
        return event;
    }

    void click(UI::Point point)
    {
        board.mouseDown(pressAt(point));
        board.mouseUp(pressAt(point));
    }

    void drag(UI::Point from, UI::Point to)
    {
        board.mouseDown(pressAt(from));
        auto moving = pressAt(from);
        moving.position = to;
        board.mouseDrag(moving);
        board.mouseUp(moving);
    }

    AppConfig config;
    TaskBoard board {config};
    UI::ComponentHost host;
    std::filesystem::path storeDirectory;
    std::vector<std::string> shownPanes;
    std::vector<std::string> toldPanes;
};
} // namespace

auto tPointFindsCardAndColumn = test("TaskBoardView/pointFindsCardAndColumn") = []
{
    auto rendered = RenderedBoard {};

    if (!rendered.render())
        return;

    auto busy = rendered.board.findCardBounds("busy");
    check(busy.has_value());

    auto card = rendered.board.findCardAt(rendered.centreOf(*busy));
    check(card && card->key == "busy" && card->zone == Zone::working);

    auto done = rendered.board.findColumnBounds(Zone::done);
    check(done.has_value());
    check(rendered.board.findColumnAt(rendered.centreOf(*done)) == Zone::done);
    check(!rendered.board.findCardAt(rendered.centreOf(*done)));
    check(!rendered.board.findColumnAt({1.f, 1.f}));
};

auto tClickSelectsThenSecondClickGoesToPane = test("TaskBoardView/clickSelectsThenSecondClickGoesToPane") = []
{
    auto rendered = RenderedBoard {};

    if (!rendered.render())
        return;

    auto other = rendered.centreOf(*rendered.board.findCardBounds("other"));
    rendered.click(other);
    check(rendered.shownPanes.empty());

    rendered.click(other);
    check(rendered.shownPanes == std::vector<std::string> {"pane-other"});
};

auto tDoubleClickGoesToPane = test("TaskBoardView/doubleClickGoesToPane") = []
{
    auto rendered = RenderedBoard {};

    if (!rendered.render())
        return;

    auto busy = rendered.centreOf(*rendered.board.findCardBounds("busy"));
    rendered.board.mouseDoubleClick(rendered.pressAt(busy));
    check(rendered.shownPanes == std::vector<std::string> {"pane-busy"});
};

auto tDragMovesAndTellsTheAgent = test("TaskBoardView/dragMovesAndTellsTheAgent") = []
{
    auto rendered = RenderedBoard {};

    if (!rendered.render())
        return;

    auto from = rendered.centreOf(*rendered.board.findCardBounds("busy"));
    auto to = rendered.centreOf(*rendered.board.findColumnBounds(Zone::review));

    rendered.board.mouseDown(rendered.pressAt(from));
    auto moving = rendered.pressAt(from);
    moving.position = to;
    rendered.board.mouseDrag(moving);
    check(rendered.board.getDragTargetZone() == Zone::review);
    rendered.board.mouseUp(moving);

    check(!rendered.board.getDragTargetZone());
    check(rendered.toldPanes
          == std::vector<std::string> {
              "pane-busy: Jamie moved this to review. Stop editing; Jamie is reviewing your diff in dv."});
    check(rendered.shownPanes.empty());

    auto database = openBoardDatabase(FilePath {rendered.storeDirectory.string()});
    check(getStoredZone(openBoardDocument(database).get(), "busy") == Zone::review);
};

auto tDragToNeedsYouMovesNothing = test("TaskBoardView/dragToNeedsYouMovesNothing") = []
{
    auto rendered = RenderedBoard {};

    if (!rendered.render())
        return;

    rendered.drag(rendered.centreOf(*rendered.board.findCardBounds("busy")),
                  rendered.centreOf(*rendered.board.findColumnBounds(Zone::needsYou)));

    check(rendered.toldPanes.empty());
};

auto tPickerRowClickPicksProject = test("TaskBoardView/pickerRowClickPicksProject") = []
{
    auto rendered = RenderedBoard {};
    rendered.board.showProjectPicker({{"eacp", "", "/p/eacp"}, {"epm", "", "/p/epm"}}, "");

    if (!rendered.render())
        return;

    check(rendered.board.findPickerRowAt({1.f, 1.f}) == std::nullopt);

    for (auto y = 0.f; y < boardHeight; y += 4.f)
    {
        if (rendered.board.findPickerRowAt({boardWidth / 2.f, y}) == std::size_t {1})
        {
            rendered.board.mouseDown(rendered.pressAt({boardWidth / 2.f, y}));
            break;
        }
    }

    check(rendered.board.isNamingTask());
};

auto tPullRequestsGoToTheirAgent = test("TaskBoardView/pullRequestsGoToTheirAgent") = []
{
    auto rendered = RenderedBoard {};
    auto resumed = std::vector<std::string> {};
    auto opened = std::vector<std::string> {};
    rendered.board.onResumeSession = [&](const std::string& directory, const std::string& sessionId)
    { resumed.push_back(directory + " " + sessionId); };
    rendered.board.onOpenUrl = [&](const std::string& url) { opened.push_back(url); };

    auto live = LinkedPullRequest {};
    live.pullRequest.url = "https://github.com/a/b/pull/1";
    live.session = RecentSession {};
    live.session->sessionId = "live-session";
    live.rule = PullRequestLinkRule::headBranch;

    auto ended = live;
    ended.pullRequest.url = "https://github.com/a/b/pull/2";
    ended.session->sessionId = "ended-session";
    ended.session->workingDirectory = "/p/ended";

    auto snapshot = BoardSnapshot {};
    auto card = makeCard("busy", Zone::working, SessionActivity::busy);
    card.sessionIds = {"live-session"};
    snapshot.cards = {card};
    rendered.board.showSnapshot(snapshot);
    rendered.board.showPullRequests(PullRequestWindow::today, {live, ended});

    if (!rendered.render())
        return;

    auto pressO = UI::KeyEvent {};
    pressO.charactersIgnoringModifiers = "o";
    rendered.board.keyDown(pressO);
    check(opened == std::vector<std::string> {"https://github.com/a/b/pull/1"});

    rendered.board.keyDown(UI::KeyEvent {.keyCode = UI::KeyCode::Return});
    check(rendered.shownPanes == std::vector<std::string> {"pane-busy"});

    rendered.board.showPullRequests(PullRequestWindow::today, {live, ended});
    rendered.render();

    for (auto y = 0.f; y < boardHeight; y += 4.f)
    {
        auto press = rendered.pressAt({boardWidth / 2.f, y});
        rendered.board.mouseDown(press);

        if (!resumed.empty())
            break;
    }

    check(resumed == std::vector<std::string> {"/p/ended ended-session"});
};
