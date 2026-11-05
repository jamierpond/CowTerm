#include "PullRequests.h"

#include "GitBranch.h"

#include <JsonFields.h>
#include <Sessions.h>

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Files.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>

namespace term
{
namespace
{
constexpr auto secondsPerDay = std::time_t {86400};
constexpr auto searchResultLimit = 100;
constexpr auto dateBufferSize = std::size_t {16};
constexpr auto pullRequestUrlStart = std::string_view {"https://github.com/"};
constexpr auto pullRequestPathMarker = std::string_view {"/pull/"};
constexpr auto claudeLinkRecordMarker = std::string_view {"\"type\":\"pr-link\""};
constexpr auto transcriptExtension = std::string_view {".jsonl"};

struct WindowDescription
{
    PullRequestWindow window;
    std::string_view name;
    std::string_view argument;
    std::time_t daysBeforeToday;
};

constexpr auto windowDescriptions = std::array {
    WindowDescription {PullRequestWindow::today, "today", "today", 0},
    WindowDescription {PullRequestWindow::threeDays, "3 days", "3d", 2},
    WindowDescription {PullRequestWindow::week, "week", "week", 6},
};

const WindowDescription& describeWindow(PullRequestWindow window)
{
    return *std::ranges::find(windowDescriptions, window, &WindowDescription::window);
}

struct LinkRuleDescription
{
    PullRequestLinkRule rule;
    std::string_view name;
};

constexpr auto linkRuleDescriptions = std::array {
    LinkRuleDescription {PullRequestLinkRule::headBranch, "branch"},
    LinkRuleDescription {PullRequestLinkRule::claudeLink, "pr-link"},
    LinkRuleDescription {PullRequestLinkRule::transcriptMention, "mentioned"},
};

std::string toLowerCase(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return text;
}

bool isPullRequestUrlCharacter(char character)
{
    auto byte = (unsigned char) character;
    return std::isalnum(byte) != 0 || character == '/' || character == '-' || character == '_'
           || character == '.';
}

// Trailing digits only: ".../pull/12/files" and ".../pull/12." both mean #12.
std::optional<std::string> trimToPullRequestUrl(std::string_view candidate)
{
    auto marker = candidate.find(pullRequestPathMarker);

    if (marker == std::string_view::npos)
        return std::nullopt;

    auto numberStart = marker + pullRequestPathMarker.size();
    auto numberEnd = numberStart;

    while (numberEnd < candidate.size() && std::isdigit((unsigned char) candidate[numberEnd]) != 0)
        ++numberEnd;

    if (numberEnd == numberStart)
        return std::nullopt;

    return std::string {candidate.substr(0, numberEnd)};
}

std::string makeSearchQuery(PullRequestWindow window, std::time_t now)
{
    auto start = getPullRequestWindowStart(window, now);
    auto parts = std::tm {};
    localtime_r(&start, &parts);
    auto date = std::array<char, dateBufferSize> {};
    auto written = std::strftime(date.data(), date.size(), "%Y-%m-%d", &parts);
    return "is:pr is:open author:@me updated:>=" + std::string {date.data(), written};
}

// `git remote -v` lines are "<name>\t<url> (fetch)".
std::set<std::string> readRemoteSlugs(const std::string& gitExecutable, const std::string& directory)
{
    auto slugs = std::set<std::string> {};
    auto result = eacp::Processes::run(gitExecutable, {"-C", directory, "remote", "-v"});

    if (!result.launched || result.exitCode != 0)
        return slugs;

    auto lines = std::istringstream {result.output};
    auto line = std::string {};

    while (std::getline(lines, line))
    {
        auto urlStart = line.find('\t');
        auto urlEnd = line.find(' ', urlStart);

        if (urlStart == std::string::npos)
            continue;

        auto slug = slugFromRemote(line.substr(urlStart + 1, urlEnd - urlStart - 1));

        if (!slug.empty())
            slugs.insert(std::move(slug));
    }

    return slugs;
}

std::optional<RecentSession> pickNewest(const std::vector<RecentSession>& sessions,
                                        const std::function<bool(const RecentSession&)>& matches)
{
    auto newest = std::optional<RecentSession> {};

    for (const auto& session: sessions)
    {
        if (!matches(session))
            continue;

        if (!newest || session.lastTimestamp > newest->lastTimestamp)
            newest = session;
    }

    return newest;
}
} // namespace

std::string_view getPullRequestWindowName(PullRequestWindow window)
{
    return describeWindow(window).name;
}

std::optional<PullRequestWindow> parsePullRequestWindow(std::string_view text)
{
    if (text == "7d")
        return PullRequestWindow::week;

    auto found = std::ranges::find(windowDescriptions, text, &WindowDescription::argument);

    if (found == windowDescriptions.end())
        return std::nullopt;

    return found->window;
}

PullRequestWindow getNextPullRequestWindow(PullRequestWindow window)
{
    auto found = std::ranges::find(windowDescriptions, window, &WindowDescription::window);
    auto next = std::next(found);

    if (next == windowDescriptions.end())
        return windowDescriptions.front().window;

    return next->window;
}

std::time_t getPullRequestWindowStart(PullRequestWindow window, std::time_t now)
{
    auto parts = std::tm {};
    localtime_r(&now, &parts);
    parts.tm_hour = 0;
    parts.tm_min = 0;
    parts.tm_sec = 0;
    parts.tm_isdst = -1;
    return std::mktime(&parts) - describeWindow(window).daysBeforeToday * secondsPerDay;
}

std::vector<PullRequest> parsePullRequestSearch(std::string_view json)
{
    using jsonfields::readField;
    using jsonfields::readStringField;

    auto pullRequests = std::vector<PullRequest> {};
    auto value = Miro::Json::getParsedValue(json);
    const auto& nodes = readField(readField(readField(value, "data"), "search"), "nodes");

    if (!nodes.isArray())
        return pullRequests;

    for (const auto& node: nodes.asArray())
    {
        auto pullRequest = PullRequest {};
        const auto& number = readField(node, "number");

        if (!number.isNumber())
            continue;

        pullRequest.number = (int) number.asNumber();
        pullRequest.title = readStringField(node, "title");
        pullRequest.url = readStringField(node, "url");
        pullRequest.updatedAt = readStringField(node, "updatedAt");
        pullRequest.repository = readStringField(readField(node, "repository"), "nameWithOwner");
        pullRequest.headRepository = readStringField(readField(node, "headRepository"), "nameWithOwner");
        pullRequest.headBranch = readStringField(node, "headRefName");
        pullRequest.reviewDecision = readStringField(node, "reviewDecision");

        const auto& commits = readField(readField(node, "commits"), "nodes");

        if (commits.isArray() && !commits.asArray().empty())
        {
            const auto& rollup = readField(readField(commits.asArray().front(), "commit"), "statusCheckRollup");
            pullRequest.checksState = readStringField(rollup, "state");
        }

        pullRequests.push_back(std::move(pullRequest));
    }

    return pullRequests;
}

PullRequestFetch fetchPullRequests(const std::string& ghExecutable,
                                   PullRequestWindow window,
                                   std::time_t now)
{
    constexpr auto graphQuery = std::string_view {
        "query($q:String!,$n:Int!){search(query:$q,type:ISSUE,first:$n){nodes{... on PullRequest{"
        "number title url updatedAt repository{nameWithOwner} headRefName "
        "headRepository{nameWithOwner} reviewDecision "
        "commits(last:1){nodes{commit{statusCheckRollup{state}}}}}}}}"};

    auto result = eacp::Processes::run(ghExecutable,
                                       {"api",
                                        "graphql",
                                        "-f",
                                        "query=" + std::string {graphQuery},
                                        "-f",
                                        "q=" + makeSearchQuery(window, now),
                                        "-F",
                                        "n=" + std::to_string(searchResultLimit)});

    auto fetch = PullRequestFetch {};

    if (!result.launched)
        fetch.problem = "gh did not start";
    else if (result.exitCode != 0)
        fetch.problem = "gh api graphql failed";
    else
        fetch.pullRequests = parsePullRequestSearch(result.output);

    return fetch;
}

std::set<std::string> findPullRequestUrls(std::string_view text)
{
    auto urls = std::set<std::string> {};
    auto position = text.find(pullRequestUrlStart);

    while (position != std::string_view::npos)
    {
        auto end = position + pullRequestUrlStart.size();

        while (end < text.size() && isPullRequestUrlCharacter(text[end]))
            ++end;

        if (auto url = trimToPullRequestUrl(text.substr(position, end - position)))
            urls.insert(*url);

        position = text.find(pullRequestUrlStart, end);
    }

    return urls;
}

std::set<std::string> findClaudeLinkedPullRequestUrls(std::string_view transcriptText)
{
    auto urls = std::set<std::string> {};
    auto lineStart = std::size_t {0};

    while (lineStart < transcriptText.size())
    {
        auto lineEnd = transcriptText.find('\n', lineStart);

        if (lineEnd == std::string_view::npos)
            lineEnd = transcriptText.size();

        auto line = transcriptText.substr(lineStart, lineEnd - lineStart);

        if (line.find(claudeLinkRecordMarker) != std::string_view::npos)
            urls.merge(findPullRequestUrls(line));

        lineStart = lineEnd + 1;
    }

    return urls;
}

std::vector<RecentSession> collectRecentSessions(const std::string& configDirectory,
                                                 const std::string& gitExecutable,
                                                 std::time_t since)
{
    namespace fs = std::filesystem;

    auto sessions = std::vector<RecentSession> {};
    auto remotesByDirectory = std::map<std::string, std::set<std::string>> {};
    auto error = std::error_code {};
    auto projects = fs::path {configDirectory} / "projects";

    for (const auto& folder: fs::directory_iterator {projects, error})
    {
        for (const auto& file: fs::directory_iterator {folder.path(), error})
        {
            if (file.path().extension() != transcriptExtension)
                continue;

            auto written = fs::last_write_time(file.path(), error);
            auto writtenOnSystemClock = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                written - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            auto writtenSeconds = std::chrono::system_clock::to_time_t(writtenOnSystemClock);

            if (error || writtenSeconds < since)
                continue;

            auto summary = claudecli::summariseTranscript(claudecli::readTranscript(file.path().string()));

            if (summary.cwd.empty())
                continue;

            auto session = RecentSession {};
            session.sessionId = summary.sessionId;
            session.workingDirectory = summary.cwd;
            session.gitBranch = summary.gitBranch;
            session.lastTimestamp = summary.lastTimestamp;
            auto text = eacp::Files::readFile(eacp::FilePath {file.path().string()});
            session.mentionedPullRequestUrls = findPullRequestUrls(text);
            session.claudeLinkedPullRequestUrls = findClaudeLinkedPullRequestUrls(text);

            if (!remotesByDirectory.contains(summary.cwd))
                remotesByDirectory[summary.cwd] = readRemoteSlugs(gitExecutable, summary.cwd);

            session.repositorySlugs = remotesByDirectory[summary.cwd];
            sessions.push_back(std::move(session));
        }
    }

    return sessions;
}

std::string_view getPullRequestLinkRuleName(PullRequestLinkRule rule)
{
    return std::ranges::find(linkRuleDescriptions, rule, &LinkRuleDescription::rule)->name;
}

std::vector<LinkedPullRequest> linkPullRequests(const std::vector<PullRequest>& pullRequests,
                                                const std::vector<RecentSession>& sessions)
{
    auto linked = std::vector<LinkedPullRequest> {};

    for (const auto& pullRequest: pullRequests)
    {
        auto link = LinkedPullRequest {};
        link.pullRequest = pullRequest;
        auto headSlug = toLowerCase(pullRequest.headRepository);

        link.session = pickNewest(sessions,
                                  [&](const RecentSession& session)
                                  {
                                      return !headSlug.empty() && session.repositorySlugs.contains(headSlug)
                                             && session.gitBranch == pullRequest.headBranch;
                                  });

        if (link.session)
        {
            link.rule = PullRequestLinkRule::headBranch;
            linked.push_back(std::move(link));
            continue;
        }

        link.session = pickNewest(sessions,
                                  [&](const RecentSession& session)
                                  { return session.claudeLinkedPullRequestUrls.contains(pullRequest.url); });

        if (link.session)
        {
            link.rule = PullRequestLinkRule::claudeLink;
            linked.push_back(std::move(link));
            continue;
        }

        link.session = pickNewest(sessions,
                                  [&](const RecentSession& session)
                                  { return session.mentionedPullRequestUrls.contains(pullRequest.url); });

        if (link.session)
            link.rule = PullRequestLinkRule::transcriptMention;

        linked.push_back(std::move(link));
    }

    return linked;
}
} // namespace term
