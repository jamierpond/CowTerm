#pragma once

#include <ctime>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
// How far back the board's pull request view looks, by last update.
enum class PullRequestWindow
{
    today,
    threeDays,
    week
};

std::string_view getPullRequestWindowName(PullRequestWindow window);

// "today", "3d", "week" (also "7d"); what `ct board prs --since` takes.
std::optional<PullRequestWindow> parsePullRequestWindow(std::string_view text);
PullRequestWindow getNextPullRequestWindow(PullRequestWindow window);

// The window's first second: local midnight today, two days before, six before.
std::time_t getPullRequestWindowStart(PullRequestWindow window, std::time_t now);

struct PullRequest
{
    int number = 0;
    std::string title;
    std::string url;
    std::string repository;
    std::string headRepository;
    std::string headBranch;
    // GitHub's rollup: SUCCESS, FAILURE, PENDING, ERROR, EXPECTED; empty with no checks.
    std::string checksState;
    // APPROVED, CHANGES_REQUESTED, REVIEW_REQUIRED; empty when none is needed.
    std::string reviewDecision;
    std::string updatedAt;
};

// The `gh api graphql` search answer that fetchPullRequests asks for.
std::vector<PullRequest> parsePullRequestSearch(std::string_view json);

// Blocking: one GraphQL search for Jamie's open pull requests updated since
// the window began, in every repository. Off the main thread.
struct PullRequestFetch
{
    std::vector<PullRequest> pullRequests;
    std::string problem;
};

PullRequestFetch fetchPullRequests(const std::string& ghExecutable,
                                   PullRequestWindow window,
                                   std::time_t now);

// A claude session from its transcript: where it ran, on which branch, and
// every pull request URL it mentions (gh pr create prints one).
struct RecentSession
{
    std::string sessionId;
    std::string workingDirectory;
    std::string gitBranch;
    // Every remote's "owner/repo", lowercased: a fork's origin and the upstream it pushes to.
    std::set<std::string> repositorySlugs;
    std::string lastTimestamp;
    std::set<std::string> mentionedPullRequestUrls;
    // From claude's own "pr-link" records: pull requests this session opened or adopted.
    std::set<std::string> claudeLinkedPullRequestUrls;
};

std::set<std::string> findPullRequestUrls(std::string_view text);

// The pull request URLs on a transcript's `"type":"pr-link"` lines.
std::set<std::string> findClaudeLinkedPullRequestUrls(std::string_view transcriptText);

// Blocking: every transcript under configDirectory/projects written since
// `since`, with each directory's remotes read by git. Off the main thread.
std::vector<RecentSession> collectRecentSessions(const std::string& configDirectory,
                                                 const std::string& gitExecutable,
                                                 std::time_t since);

enum class PullRequestLinkRule
{
    // The session ran on the pull request's head repository and branch.
    headBranch,
    // claude recorded the pull request against the session (a "pr-link" record).
    claudeLink,
    // The session's transcript has the pull request's URL in it.
    transcriptMention
};

std::string_view getPullRequestLinkRuleName(PullRequestLinkRule rule);

struct LinkedPullRequest
{
    PullRequest pullRequest;
    std::optional<RecentSession> session;
    std::optional<PullRequestLinkRule> rule;
};

// Head branch first, then claude's pr-link record, then a mention; the newest matching session wins either way.
std::vector<LinkedPullRequest> linkPullRequests(const std::vector<PullRequest>& pullRequests,
                                                const std::vector<RecentSession>& sessions);
} // namespace term
