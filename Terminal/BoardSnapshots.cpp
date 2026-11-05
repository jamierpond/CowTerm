#include "BoardSnapshots.h"

#include "Config.h"
#include "ProcessEnvironment.h"
#include "Protocol.h"
#include "BoardEnvironment.h"
#include "PullRequests.h"
#include "TaskBoard.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/UI/UI.h>

#include <array>
#include <cstdio>
#include <ctime>
#include <functional>
#include <vector>

namespace term
{
using namespace eacp;

namespace
{
constexpr auto snapshotFailedExitCode = 1;
constexpr auto isoTimestampBufferSize = std::size_t {32};
constexpr auto secondsPerMinute = std::int64_t {60};
constexpr auto secondsPerHour = std::int64_t {3600};

struct SnapshotShape
{
    std::string_view name;
    float width;
    float height;
};

constexpr auto snapshotShapes = std::array {
    SnapshotShape {"ultrawide", 2400.f, 900.f},
    SnapshotShape {"wide", 1400.f, 860.f},
    SnapshotShape {"laptop", 1024.f, 700.f},
    SnapshotShape {"narrow", 640.f, 900.f},
    SnapshotShape {"phone-portrait", 390.f, 844.f},
};

struct Scene
{
    std::string name;
    BoardSnapshot snapshot;
    std::string selectedKey;
    std::optional<std::string> pickerQuery;
    // Run after a first paint, through the board's own mouse handlers.
    std::function<void(TaskBoard&)> interact;
};

UI::Point getCentre(UI::Rect bounds)
{
    return {bounds.x + bounds.w / 2.0f, bounds.y + bounds.h / 2.0f};
}

// Presses the card and drags it over the column, without letting go.
void dragCardOverColumn(TaskBoard& board, const std::string& key, Zone zone)
{
    auto card = board.findCardBounds(key);
    auto column = board.findColumnBounds(zone);

    if (!card || !column)
        return;

    auto press = UI::MouseEvent {};
    press.position = getCentre(*card);
    press.downPosition = press.position;
    board.mouseDown(press);

    auto drag = press;
    drag.position = getCentre(*column);
    board.mouseDrag(drag);
}

std::vector<Project> makeFixtureProjects()
{
    return {{"eacp", "eyalamirmusic/eacp", "/Users/jamie/projects/eacp"},
            {"CowTerm", "jamierpond/CowTerm", "/Users/jamie/projects/CowTerm"},
            {"tamber-web", "Tamber-Inc/tamber-web", "/Users/jamie/projects/tamber-web"},
            {"emberstore", "Tamber-Inc/emberstore", ""},
            {"epm", "jamierpond/epm", ""}};
}

std::string formatTimestampSecondsAgo(std::int64_t secondsAgo)
{
    auto seconds = (std::time_t) (std::time(nullptr) - secondsAgo);
    auto parts = std::tm {};
    gmtime_r(&seconds, &parts);
    auto buffer = std::array<char, isoTimestampBufferSize> {};
    auto written = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &parts);
    return {buffer.data(), written};
}

TaskCard makeFixtureCard(const std::string& repository,
                         const std::string& branch,
                         const std::string& description,
                         const std::string& badge,
                         SessionActivity activity,
                         Zone zone,
                         std::int64_t secondsAgo)
{
    auto card = TaskCard {};
    card.workingDirectory = "/Users/jamie/projects/" + repository;
    card.gitBranch = branch;
    card.key = makeCardKey(card.workingDirectory, branch);
    card.taskName = repository + " · " + branch;
    card.description = description;
    card.badgeText = badge;
    card.activity = activity;
    card.zone = zone;
    card.latestActivityTimestamp = formatTimestampSecondsAgo(secondsAgo);
    card.sessionCount = 1;
    return card;
}

BoardSnapshot makeFixtureBoard()
{
    auto board = BoardSnapshot {};
    board.cards = {
        makeFixtureCard("eacp-posix-environment-lock",
                        "jp/posix-environment-lock",
                        "Merge #193 now, or wait for #194's CI?",
                        "waiting: question",
                        SessionActivity::needsYou,
                        Zone::needsYou,
                        4 * secondsPerMinute),
        makeFixtureCard("tamber-web-eacp-crash-reporting",
                        "eacp-crash-reporting",
                        "Allow: Bash git push origin jp/crashout",
                        "waiting: permission",
                        SessionActivity::needsYou,
                        Zone::needsYou,
                        11 * secondsPerMinute),
        makeFixtureCard("epm-lockstep",
                        "lockstep",
                        "Running CI on five lanes",
                        "busy",
                        SessionActivity::busy,
                        Zone::working,
                        2 * secondsPerMinute),
        makeFixtureCard("lockstep",
                        "main",
                        "Writing v0.4.4 release notes",
                        "idle",
                        SessionActivity::idle,
                        Zone::working,
                        30),
        makeFixtureCard("eacp-core-lockstep-utils",
                        "jp/core-lockstep-utils",
                        "Core Sha256/toHex/FilePath/Files/architectureName; PR #198",
                        "idle",
                        SessionActivity::idle,
                        Zone::review,
                        secondsPerHour),
        makeFixtureCard("ClaudeCodeCpp",
                        "main",
                        "agents --json parser and transcript summaries",
                        "idle",
                        SessionActivity::idle,
                        Zone::done,
                        3 * secondsPerHour),
    };
    return board;
}

// Enough cards to overflow every column, with names longer than any card.
BoardSnapshot makeCrowdedFixtureBoard()
{
    auto board = makeFixtureBoard();
    constexpr auto extraCardCount = 14;

    for (auto index = 0; index < extraCardCount; ++index)
    {
        board.cards.push_back(makeFixtureCard(
            "tamber-web-checkout-tracking-" + std::to_string(index),
            "jp/an-extremely-long-branch-name-that-cannot-fit-on-any-card-" + std::to_string(index),
            "Wire the paddle webhook into the checkout tracking table and backfill the last thirty days of orders from the export",
            "busy",
            SessionActivity::busy,
            Zone::working,
            (index + 1) * secondsPerMinute));
    }

    return board;
}

std::vector<LinkedPullRequest> makeFixturePullRequests(const BoardSnapshot& board)
{
    struct Fixture
    {
        std::string repository;
        int number;
        std::string title;
        std::string checks;
        std::string review;
        std::optional<PullRequestLinkRule> rule;
        std::string sessionId;
        std::string directory;
    };

    const auto fixtures = std::vector<Fixture> {
        {"eyalamirmusic/eacp", 193, "Environment: serialise POSIX environment access", "SUCCESS", "REVIEW_REQUIRED",
         PullRequestLinkRule::headBranch, "live", board.cards.front().workingDirectory},
        {"Tamber-Inc/tamber-web", 1450, "Hub quits flush analytics off the message thread", "PENDING", "",
         PullRequestLinkRule::claudeLink, "ended", "/Users/jamie/projects/tamber-web-hub-update"},
        {"eyalamirmusic/epm", 1, "Lock the whole dependency tree, and key binaries on the locked sources", "FAILURE",
         "CHANGES_REQUESTED", PullRequestLinkRule::transcriptMention, "ended-2", "/Users/jamie/projects/eacp-thinking"},
        {"jamierpond/Crashout", 1, "Linux: signal-only crash capture (stage 1)", "", "APPROVED", std::nullopt, "", ""},
    };

    auto linked = std::vector<LinkedPullRequest> {};

    for (const auto& fixture: fixtures)
    {
        auto entry = LinkedPullRequest {};
        entry.pullRequest.repository = fixture.repository;
        entry.pullRequest.number = fixture.number;
        entry.pullRequest.title = fixture.title;
        entry.pullRequest.checksState = fixture.checks;
        entry.pullRequest.reviewDecision = fixture.review;
        entry.rule = fixture.rule;

        if (fixture.rule)
        {
            entry.session = RecentSession {};
            entry.session->sessionId = fixture.sessionId;
            entry.session->workingDirectory = fixture.directory;
        }

        linked.push_back(std::move(entry));
    }

    return linked;
}

std::vector<LinkedPullRequest> collectLivePullRequests(const BoardQuery& query)
{
    const auto now = std::time(nullptr);
    const auto window = PullRequestWindow::threeDays;
    const auto tools = makeStartAgentSettingsFromEnvironment();
    auto fetch = fetchPullRequests(tools.ghExecutable, window, now);
    auto sessions = collectRecentSessions(
        query.configDirectory, tools.gitExecutable, getPullRequestWindowStart(window, now));
    return linkPullRequests(fetch.pullRequests, sessions);
}

std::vector<Scene> makeScenes(const std::optional<BoardQuery>& liveQuery)
{
    auto fixture = makeFixtureBoard();
    auto crowded = makeCrowdedFixtureBoard();
    auto scenes = std::vector<Scene> {
        {"board", fixture, fixture.cards.front().key, std::nullopt, {}},
        {"board-review-selected", fixture, fixture.cards[4].key, std::nullopt, {}},
        {"crowded", crowded, crowded.cards.back().key, std::nullopt, {}},
        {"empty", BoardSnapshot {}, {}, std::nullopt, {}},
        {"picker", fixture, fixture.cards.front().key, std::string {"e"}, {}},
        {"mid-drag",
         fixture,
         fixture.cards[2].key,
         std::nullopt,
         [key = fixture.cards[2].key](TaskBoard& board) { dragCardOverColumn(board, key, Zone::review); }},
    };

    auto fixtureForPullRequests = fixture;
    fixtureForPullRequests.cards.front().sessionIds = {"live"};
    scenes.push_back({"pull-requests",
                      fixtureForPullRequests,
                      {},
                      std::nullopt,
                      [pullRequests = makeFixturePullRequests(fixtureForPullRequests)](TaskBoard& board)
                      { board.showPullRequests(PullRequestWindow::today, pullRequests); }});

    if (liveQuery)
    {
        auto live = collectLiveBoardSnapshot(*liveQuery);
        scenes.push_back({"live", live, {}, std::nullopt, {}});
        scenes.push_back({"pull-requests-live",
                          live,
                          {},
                          std::nullopt,
                          [pullRequests = collectLivePullRequests(*liveQuery)](TaskBoard& board)
                          { board.showPullRequests(PullRequestWindow::threeDays, pullRequests); }});
    }

    return scenes;
}
} // namespace

BoardSnapshot collectLiveBoardSnapshot(const BoardQuery& baseQuery)
{
    auto command = claudecli::AgentsCommand {};
    command.all = true;
    auto agentList = claudecli::listAgents(baseQuery.launch, command);

    if (!agentList.commandOutput.hasLaunched)
    {
        auto snapshot = BoardSnapshot {};
        snapshot.problem = "claude did not start";
        return snapshot;
    }

    auto query = baseQuery;
    query.findPaneOfProcess = [](std::int64_t processId)
    { return readProcessEnvironmentVariable(processId, proto::paneEnvironmentVariable); };

    for (const auto& agent: agentList.listing.agents)
    {
        if (!agent.pid)
            continue;

        if (auto pane = query.findPaneOfProcess(*agent.pid))
            query.panes.push_back({*pane, agent.cwd, true});
    }

    return buildBoardSnapshot(query, agentList.listing);
}

int renderBoardSnapshots(const std::string& outputDirectory,
                         const std::optional<BoardQuery>& liveQuery)
{
    const auto directory = FilePath {outputDirectory};
    Files::createDirectories(directory);
    const auto config = loadConfig();

    for (const auto& scene: makeScenes(liveQuery))
    {
        for (const auto& shape: snapshotShapes)
        {
            auto board = TaskBoard {config};
            auto host = UI::ComponentHost {};
            host.setRootComponent(board);
            host.setBounds({0.f, 0.f, shape.width, shape.height});
            host.resized();
            board.showSnapshot(scene.snapshot);
            board.selectCard(scene.selectedKey);

            if (scene.pickerQuery)
                board.showProjectPicker(makeFixtureProjects(), *scene.pickerQuery);

            if (scene.interact)
            {
                host.renderToImage(host.backingScale());
                scene.interact(board);
            }

            auto image = host.renderToImage(host.backingScale());

            if (!image.isValid())
                return snapshotFailedExitCode;

            auto path = directory / (scene.name + "-" + std::string {shape.name} + ".png");
            image.save(path);
            std::printf("%s\n", path.str().c_str());
        }
    }

    return 0;
}
} // namespace term
