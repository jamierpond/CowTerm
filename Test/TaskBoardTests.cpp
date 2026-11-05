#include "AgentBoard.h"
#include "BoardDocument.h"
#include "BoardProjects.h"
#include "PullRequests.h"
#include "CardText.h"

#include <Sessions.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <fstream>
#include <random>

// The task board's data: claude's agent listing plus transcripts, matched to
// CowTerm panes and grouped by directory and branch; zones and moves.

using namespace nano;
using namespace term;

namespace
{
struct ScratchConfig
{
    ScratchConfig()
    {
        auto random = std::random_device {};
        directory = std::filesystem::temp_directory_path()
                    / ("cowterm-board-tests-" + std::to_string(random()));
    }

    ~ScratchConfig() { std::filesystem::remove_all(directory); }

    void writeTranscript(const std::string& cwd,
                         const std::string& sessionId,
                         const std::string& branch,
                         const std::string& prompt) const
    {
        auto folder = std::filesystem::path {
            claudecli::getProjectTranscriptDirectory(directory.string(), cwd)};
        std::filesystem::create_directories(folder);
        auto file = std::ofstream {folder / (sessionId + ".jsonl")};
        file << R"({"type":"user","sessionId":")" << sessionId << R"(","cwd":")" << cwd
             << R"(","gitBranch":")" << branch
             << R"(","timestamp":"2026-11-04T10:00:00.000Z","message":{"role":"user","content":")"
             << prompt << "\"}}\n";
    }

    std::filesystem::path directory;
};

std::string makeAgent(int pid, const std::string& cwd, const std::string& sessionId, const std::string& status)
{
    return R"({"pid":)" + std::to_string(pid) + R"(,"cwd":")" + cwd
           + R"(","kind":"interactive","sessionId":")" + sessionId + R"(","status":")"
           + status + "\"}";
}

BoardQuery makeQuery(const ScratchConfig& config, std::vector<BoardPane> panes)
{
    auto query = BoardQuery {};
    query.configDirectory = config.directory.string();
    query.panes = std::move(panes);
    query.isProcessRunning = [](std::int64_t) { return true; };
    query.findPaneOfProcess = [](std::int64_t) { return std::optional<std::string> {}; };
    return query;
}
} // namespace

auto tGroupsByDirectoryAndBranch = test("TaskBoard/groupsByDirectoryAndBranch") = []
{
    auto config = ScratchConfig {};
    config.writeTranscript("/p/eacp", "s1", "jp/lock", "Fix the lock");
    config.writeTranscript("/p/eacp", "s2", "jp/lock", "Review the lock");
    config.writeTranscript("/p/epm", "s3", "main", "Release");

    auto listing = claudecli::parseAgentListing(
        "[" + makeAgent(1, "/p/eacp", "s1", "busy") + "," + makeAgent(2, "/p/eacp", "s2", "waiting")
        + "," + makeAgent(3, "/p/epm", "s3", "idle") + "]");
    auto query = makeQuery(config,
                           {{"pane-1", "/p/eacp", true}, {"pane-2", "/p/eacp", true}, {"pane-3", "/p/epm", true}});

    auto board = buildBoardSnapshot(query, listing);

    check(board.cards.size() == 2);
    auto eacp = std::ranges::find(board.cards, makeCardKey("/p/eacp", "jp/lock"), &TaskCard::key);
    check(eacp != board.cards.end());
    check(eacp->sessionCount == 2);
    check(eacp->zone == Zone::needsYou);
    check(eacp->paneShellId == "pane-2");
    check(eacp->taskName == "eacp · jp/lock");
};

auto tPrefersInheritedPaneOverDirectory = test("TaskBoard/prefersInheritedPaneOverDirectory") = []
{
    auto config = ScratchConfig {};
    config.writeTranscript("/p/eacp", "s1", "main", "Hello");
    auto listing = claudecli::parseAgentListing("[" + makeAgent(7, "/p/eacp", "s1", "busy") + "]");
    auto query = makeQuery(config, {{"by-directory", "/p/eacp", true}, {"inherited", "/elsewhere", false}});
    query.findPaneOfProcess = [](std::int64_t pid)
    { return pid == 7 ? std::optional<std::string> {"inherited"} : std::nullopt; };

    auto board = buildBoardSnapshot(query, listing);

    check(board.cards.size() == 1);
    check(board.cards.front().paneShellId == "inherited");
};

auto tAgentWithoutPaneIsNoCard = test("TaskBoard/agentWithoutPaneIsNoCard") = []
{
    auto config = ScratchConfig {};
    config.writeTranscript("/p/eacp", "s1", "main", "Hello");
    config.writeTranscript("/p/epm", "s2", "main", "Hello");
    auto listing = claudecli::parseAgentListing(
        "[" + makeAgent(1, "/p/eacp", "s1", "busy") + "," + makeAgent(2, "/p/epm", "s2", "busy") + "]");
    auto query = makeQuery(config, {{"shell-only", "/p/eacp", false}, {"claude", "/p/epm", true}});

    auto board = buildBoardSnapshot(query, listing);

    check(board.cards.size() == 1);
    check(board.cards.front().paneShellId == "claude");
};

auto tOnePanePerAgent = test("TaskBoard/onePanePerAgent") = []
{
    auto config = ScratchConfig {};
    config.writeTranscript("/p/eacp", "s1", "main", "One");
    config.writeTranscript("/p/eacp", "s2", "main", "Two");
    auto listing = claudecli::parseAgentListing(
        "[" + makeAgent(1, "/p/eacp", "s1", "busy") + "," + makeAgent(2, "/p/eacp", "s2", "busy") + "]");
    auto query = makeQuery(config, {{"only", "/p/eacp", true}});

    auto board = buildBoardSnapshot(query, listing);

    check(board.cards.size() == 1);
    check(board.cards.front().sessionCount == 1);
};

auto tEndedAndEmptySessionsAreNoCards = test("TaskBoard/endedAndEmptySessionsAreNoCards") = []
{
    auto config = ScratchConfig {};
    config.writeTranscript("/p/eacp", "s1", "main", "Hello");
    auto listing = claudecli::parseAgentListing(
        R"([{"cwd":"/p/eacp","kind":"interactive","sessionId":"s1","status":"idle"},)"
        + makeAgent(2, "/p/eacp", "no-transcript", "idle") + "]");
    auto query = makeQuery(config, {{"pane", "/p/eacp", true}});

    check(buildBoardSnapshot(query, listing).cards.empty());
};

auto tStoredZoneShownUnlessNeedsYou = test("TaskBoard/storedZoneShownUnlessNeedsYou") = []
{
    auto board = BoardSnapshot {};
    board.cards = {TaskCard {.key = "a@main", .zone = Zone::working, .activity = SessionActivity::busy},
                   TaskCard {.key = "b@main", .zone = Zone::needsYou, .activity = SessionActivity::needsYou}};
    auto document = BoardDocument {};
    moveCard(document, "a@main", Zone::review, MoveRequester::jamie);
    moveCard(document, "b@main", Zone::done, MoveRequester::jamie);

    applyBoardDocument(board, document);

    check(board.cards[0].zone == Zone::review);
    check(board.cards[1].zone == Zone::needsYou);
};

auto tAgentMovesOnlyToReviewOrDone = test("TaskBoard/agentMovesOnlyToReviewOrDone") = []
{
    auto document = BoardDocument {};

    check(moveCard(document, "a@main", Zone::working, MoveRequester::agent)
          == MoveOutcome::refusedForAgent);
    check(moveCard(document, "a@main", Zone::review, MoveRequester::agent) == MoveOutcome::moved);
    check(moveCard(document, "a@main", Zone::review, MoveRequester::agent) == MoveOutcome::unchanged);
    check(moveCard(document, "a@main", Zone::working, MoveRequester::jamie) == MoveOutcome::moved);
};

auto tMoveMessagesSayWhatTheMoveMeans = test("TaskBoard/moveMessagesSayWhatTheMoveMeans") = []
{
    check(getZoneMessageForAgent(Zone::review)
          == "Jamie moved this to review. Stop editing; Jamie is reviewing your diff in dv.");
    check(getZoneMessageForAgent(Zone::needsYou).empty());
    check(findZoneByMoveLetter('d') == Zone::done);
    check(!findZoneByMoveLetter('x'));
};

auto tBadgeHiddenWhenItRepeatsColumn = test("TaskBoard/badgeHiddenWhenItRepeatsColumn") = []
{
    check(getShownCardBadge(TaskCard {.badgeText = "busy", .zone = Zone::working}).empty());
    check(getShownCardBadge(TaskCard {.badgeText = "busy", .zone = Zone::review}) == "busy");
};

auto tBranchNameFromTitle = test("TaskBoard/branchNameFromTitle") = []
{
    check(makeTaskBranchName("Fix the auth redirect!") == "jp/fix-the-auth-redirect");
    check(makeWorktreePath("/p", Project {"eacp", "", "/p/eacp"}, "jp/fix") == "/p/eacp-jp-fix");
};

auto tPickerUsesPaletteFuzzyScore = test("TaskBoard/pickerUsesPaletteFuzzyScore") = []
{
    auto projects = std::vector<Project> {{"eacp", "", "/p/eacp"}, {"CowTerm", "", "/p/CowTerm"}};
    auto shown = filterProjects(projects, "cow");

    check(shown.size() == 1);
    check(shown.front().name == "CowTerm");
};

// A scratch repository whose origin is a local bare one: the worktree comes off
// origin's default branch, named from the title.
auto tPreparesWorktreeOffOriginDefault = test("TaskBoard/preparesWorktreeOffOriginDefault") = []
{
    auto scratch = ScratchConfig {};
    const auto root = scratch.directory;
    const auto run = [](const std::string& command) { return std::system(command.c_str()); };
    std::filesystem::create_directories(root / "projects");

    check(run("git init -q -b main " + (root / "origin.git").string() + " --bare") == 0);
    check(run("git clone -q " + (root / "origin.git").string() + " " + (root / "projects/demo").string()
              + " 2>/dev/null") == 0);
    check(run("cd " + (root / "projects/demo").string()
              + " && git -c user.name=t -c user.email=t@t commit -q --allow-empty -m one && git push -q origin main")
          == 0);

    auto request = WorkspaceRequest {};
    request.project = Project {"demo", "", (root / "projects/demo").string()};
    request.projectsDirectory = (root / "projects").string();
    request.taskBranch = makeTaskBranchName("Try it");
    request.gitExecutable = "git";
    request.ghExecutable = "gh";

    auto steps = std::vector<std::string> {};
    auto prepared = prepareWorkspace(request, [&](const std::string& step) { steps.push_back(step); });

    check(prepared.hasSucceeded);
    check(prepared.defaultBranch == "main");
    check(prepared.worktreePath == (root / "projects/demo-jp-try-it").string());
    check(std::filesystem::exists(root / "projects/demo-jp-try-it/.git"));
};

namespace
{
PullRequest makePullRequest(const std::string& repository, int number, const std::string& branch)
{
    auto pullRequest = PullRequest {};
    pullRequest.repository = repository;
    pullRequest.headRepository = repository;
    pullRequest.number = number;
    pullRequest.headBranch = branch;
    pullRequest.url = "https://github.com/" + repository + "/pull/" + std::to_string(number);
    return pullRequest;
}

RecentSession makeSession(const std::string& id,
                          const std::string& slug,
                          const std::string& branch,
                          const std::string& lastTimestamp)
{
    auto session = RecentSession {};
    session.sessionId = id;
    session.workingDirectory = "/p/" + id;
    session.repositorySlugs = {slug};
    session.gitBranch = branch;
    session.lastTimestamp = lastTimestamp;
    return session;
}
} // namespace

auto tHeadBranchLinksFirst = test("PullRequests/headBranchLinksFirst") = []
{
    auto pullRequest = makePullRequest("Tamber-Inc/tamber-web", 7, "jp/fix");
    auto onBranch = makeSession("on-branch", "tamber-inc/tamber-web", "jp/fix", "2026-11-04T09:00:00");
    auto claimed = makeSession("claimed", "tamber-inc/tamber-web", "main", "2026-11-04T10:00:00");
    claimed.claudeLinkedPullRequestUrls = {pullRequest.url};

    auto linked = linkPullRequests({pullRequest}, {claimed, onBranch});

    check(linked.front().session->sessionId == "on-branch");
    check(linked.front().rule == PullRequestLinkRule::headBranch);
};

auto tBranchMatchNeedsTheRepository = test("PullRequests/branchMatchNeedsTheRepository") = []
{
    auto pullRequest = makePullRequest("eyalamirmusic/eacp", 1, "main");
    auto otherRepository = makeSession("other", "jamierpond/cowterm", "main", "2026-11-04T09:00:00");

    check(!linkPullRequests({pullRequest}, {otherRepository}).front().session);
};

auto tForkWithUpstreamRemoteMatches = test("PullRequests/forkWithUpstreamRemoteMatches") = []
{
    auto pullRequest = makePullRequest("eyalamirmusic/eacp", 1, "jp/lock");
    auto fork = makeSession("fork", "jamierpond/eacp", "jp/lock", "2026-11-04T09:00:00");
    fork.repositorySlugs.insert("eyalamirmusic/eacp");

    check(linkPullRequests({pullRequest}, {fork}).front().rule == PullRequestLinkRule::headBranch);
};

auto tClaudeLinkBeatsMention = test("PullRequests/claudeLinkBeatsMention") = []
{
    auto pullRequest = makePullRequest("eyalamirmusic/eacp", 2, "jp/x");
    auto mentioner = makeSession("mentioner", "", "main", "2026-11-04T11:00:00");
    mentioner.mentionedPullRequestUrls = {pullRequest.url};
    auto creator = makeSession("creator", "", "main", "2026-11-04T09:00:00");
    creator.claudeLinkedPullRequestUrls = {pullRequest.url};
    creator.mentionedPullRequestUrls = {pullRequest.url};

    auto linked = linkPullRequests({pullRequest}, {mentioner, creator}).front();

    check(linked.session->sessionId == "creator");
    check(linked.rule == PullRequestLinkRule::claudeLink);
};

auto tNewestMentionWins = test("PullRequests/newestMentionWins") = []
{
    auto pullRequest = makePullRequest("eyalamirmusic/eacp", 3, "jp/x");
    auto older = makeSession("older", "", "main", "2026-11-04T08:00:00");
    auto newer = makeSession("newer", "", "main", "2026-11-04T12:00:00");
    older.mentionedPullRequestUrls = newer.mentionedPullRequestUrls = {pullRequest.url};

    auto linked = linkPullRequests({pullRequest}, {older, newer}).front();

    check(linked.session->sessionId == "newer");
    check(linked.rule == PullRequestLinkRule::transcriptMention);
};

auto tUnlinkedPullRequestHasNoSession = test("PullRequests/unlinkedPullRequestHasNoSession") = []
{
    auto linked = linkPullRequests({makePullRequest("a/b", 4, "x")}, {}).front();

    check(!linked.session && !linked.rule);
};

auto tFindsPullRequestUrlsInText = test("PullRequests/findsPullRequestUrlsInText") = []
{
    auto urls = findPullRequestUrls(
        R"(made https://github.com/a/b/pull/12. see https://github.com/a/b/pull/12/files and https://github.com/a/b/issues/3)");

    check(urls == std::set<std::string> {"https://github.com/a/b/pull/12"});

    auto claudeLinks = findClaudeLinkedPullRequestUrls(
        "{\"type\":\"user\",\"x\":\"https://github.com/a/b/pull/1\"}\n"
        "{\"type\":\"pr-link\",\"prUrl\":\"https://github.com/a/b/pull/2\"}\n");

    check(claudeLinks == std::set<std::string> {"https://github.com/a/b/pull/2"});
};

auto tParsesGraphQlSearch = test("PullRequests/parsesGraphQlSearch") = []
{
    auto pullRequests = parsePullRequestSearch(
        R"({"data":{"search":{"nodes":[{"number":1450,"title":"Hub quits","url":"https://github.com/Tamber-Inc/tamber-web/pull/1450",)"
        R"("updatedAt":"2026-11-04T20:26:40Z","repository":{"nameWithOwner":"Tamber-Inc/tamber-web"},"headRefName":"jp/hub",)"
        R"("headRepository":{"nameWithOwner":"Tamber-Inc/tamber-web"},"reviewDecision":null,)"
        R"("commits":{"nodes":[{"commit":{"statusCheckRollup":{"state":"SUCCESS"}}}]}}]}}})");

    check(pullRequests.size() == 1);
    check(pullRequests.front().number == 1450);
    check(pullRequests.front().headBranch == "jp/hub");
    check(pullRequests.front().checksState == "SUCCESS");
    check(pullRequests.front().reviewDecision.empty());
};

auto tWindowsCycleAndParse = test("PullRequests/windowsCycleAndParse") = []
{
    check(parsePullRequestWindow("3d") == PullRequestWindow::threeDays);
    check(parsePullRequestWindow("7d") == PullRequestWindow::week);
    check(!parsePullRequestWindow("month"));
    check(getNextPullRequestWindow(PullRequestWindow::week) == PullRequestWindow::today);

    auto now = std::time(nullptr);
    check(getPullRequestWindowStart(PullRequestWindow::today, now) <= now);
    check(getPullRequestWindowStart(PullRequestWindow::week, now)
          < getPullRequestWindowStart(PullRequestWindow::threeDays, now));
};
