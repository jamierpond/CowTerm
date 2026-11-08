#include "PrDashboard.h"
#include "TextLayout.h"

#include "Command.h"
#include "FuzzyMatch.h"
#include "Projects.h"

#include <eacp/Core/App/Clipboard.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Files.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <ctime>
#include <filesystem>
#include <thread>

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
constexpr auto listMetrics = ListPanelMetrics {.maximumWidth = 960.0f,
                                               .rowHeight = 32.0f,
                                               .headerHeight = 46.0f,
                                               .hintHeight = 24.0f,
                                               .maximumRows = 14,
                                               .topFraction = 0.12f};
constexpr auto fontSizes = OverlayFontSizes {.header = 16.0f, .row = 14.0f, .detail = 12.0f};

// Right-hand columns: the owning session, and review state before it.
constexpr float sessionColumn = 130.0f;
constexpr float reviewColumn = 150.0f;

constexpr auto prFieldCount = std::size_t {10};
constexpr auto statusLength = 60;

constexpr auto textLeft = 18.0f;
constexpr auto headerBaseline = 30.0f;
constexpr auto queryLeft = 66.0f;
constexpr auto statusRightPadding = 16.0f;
constexpr auto rowSideInset = 8.0f;
constexpr auto rowGap = 2.0f;
constexpr auto baselineFraction = 0.62f;
constexpr auto ciDotLeft = 12.0f;
constexpr auto ageLeft = 34.0f;
constexpr auto repoLeft = 76.0f;
constexpr auto repoNameLength = 18;
constexpr auto titleLeft = 226.0f;
constexpr auto titleRightPadding = 24.0f;
constexpr auto minimumTitleLength = 8.0f;
constexpr auto averageTitleCharacterWidth = 7.2f;
constexpr auto draftTitleAlpha = 0.6f;
constexpr auto readyTitleAlpha = 1.0f;
constexpr auto reviewLength = 20;
constexpr auto columnRightPadding = 12.0f;
constexpr auto sessionNameLength = 16;
constexpr auto hintBottomOffset = 10.0f;

constexpr auto secondsPerMinute = 60;
constexpr auto secondsPerHour = 3600;
constexpr auto secondsPerDay = 86400;
constexpr auto daysPerWeek = 7;
constexpr auto daysShownBeforeWeeks = 14;

std::string lowered(std::string text)
{
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    return text;
}

bool isTrailingWhitespace(char c)
{
    return c == '\n' || c == '\r' || c == ' ';
}

// scp-like: git@github.com:owner/repo.git
bool isScpLikeRemote(const std::string& url)
{
    const auto colon = url.find(':');
    return colon != std::string::npos && url.find('/') > colon;
}

// The path part of a remote URL, or nothing for a "scheme://host" with no path.
std::optional<std::string> stripRemoteHost(const std::string& url)
{
    if (const auto scheme = url.find("://"); scheme != std::string::npos)
    {
        const auto afterScheme = url.substr(scheme + 3);
        const auto slash = afterScheme.find('/');

        if (slash == std::string::npos)
            return std::nullopt;

        return afterScheme.substr(slash + 1);
    }

    if (isScpLikeRemote(url))
        return url.substr(url.find(':') + 1);

    return url;
}

// "owner/repo" from any git remote URL form (https, ssh://, scp-like),
// lowercased, ".git" stripped. Empty when it doesn't look like a remote.
std::string slugFromRemote(std::string url)
{
    while (!url.empty() && isTrailingWhitespace(url.back()))
        url.pop_back();

    auto path = stripRemoteHost(url);

    if (!path)
        return {};

    while (!path->empty() && path->front() == '/')
        path->erase(path->begin());

    if (path->ends_with(".git"))
        *path = path->substr(0, path->size() - 4);

    if (path->find('/') == std::string::npos)
        return {};

    return lowered(*path);
}

CommandOutput readOriginRemote(const std::string& dir)
{
    return runViaLoginShell("git -C " + shellQuote(dir) + " remote get-url origin 2>/dev/null");
}

// repo slug -> {session key, session name} for each {key, name, project dir};
// the first session on a repo wins.
std::map<std::string, std::pair<std::string, std::string>>
    resolveSessionsByRepo(const std::vector<std::array<std::string, 3>>& snapshot)
{
    auto map = std::map<std::string, std::pair<std::string, std::string>> {};

    for (const auto& [key, name, dir]: snapshot)
    {
        const auto remote = readOriginRemote(dir);

        if (remote.status != 0)
            continue;

        const auto slug = slugFromRemote(remote.text);

        if (!slug.empty() && !map.contains(slug))
            map[slug] = {key, name};
    }

    return map;
}

// Seconds since the epoch for gh's "2026-07-19T12:34:56Z" timestamps.
// Days-from-civil arithmetic, so no timegm portability games.
std::int64_t epochFromIso(const std::string& iso)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;

    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6)
        return 0;

    const auto year = (std::int64_t) y - (mo <= 2 ? 1 : 0);
    const auto era = (year >= 0 ? year : year - 399) / 400;
    const auto yoe = year - era * 400;
    const auto doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const auto days = era * 146097 + doe - 719468;

    return days * 86400 + h * 3600 + mi * 60 + s;
}

std::string formatAge(std::int64_t seconds)
{
    if (seconds < 0)
        seconds = 0;

    if (seconds >= daysShownBeforeWeeks * secondsPerDay)
        return std::to_string(seconds / (daysPerWeek * secondsPerDay)) + "w";

    if (seconds >= secondsPerDay)
        return std::to_string(seconds / secondsPerDay) + "d";

    if (seconds >= secondsPerHour)
        return std::to_string(seconds / secondsPerHour) + "h";

    return std::to_string(seconds / secondsPerMinute) + "m";
}

// gojq's @tsv escapes \ t n r; fold them back.
std::string tsvUnescape(const std::string& text)
{
    auto out = std::string {};
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '\\' || i + 1 >= text.size())
        {
            out += text[i];
            continue;
        }

        switch (text[++i])
        {
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case '\\': out += '\\'; break;
            default: out += text[i]; break;
        }
    }

    return out;
}

// One gh call for the whole board. gh's embedded jq flattens the GraphQL
// response into one TSV row per PR, so no JSON parsing happens in-app.
// Fields: ci, pushedAt, repo, number, draft, reviewDecision, reviewers,
// branch, url, title.
std::string fetchCommand()
{
    const auto graphql =
        "query { search(query:\"author:@me state:open type:pr\", type:ISSUE, first:100) {"
        " nodes { ... on PullRequest {"
        " number title url isDraft headRefName createdAt"
        " repository { nameWithOwner }"
        " reviewDecision"
        " reviewRequests(first:10) { nodes { requestedReviewer {"
        " ... on User { login } ... on Team { name } } } }"
        " commits(last:1) { nodes { commit {"
        " committedDate pushedDate statusCheckRollup { state } } } }"
        " } } } }";

    const auto jq =
        ".data.search.nodes[] | select(.url != null) | ["
        " (.commits.nodes[0].commit.statusCheckRollup.state // \"NONE\"),"
        " (.commits.nodes[0].commit.pushedDate"
        "   // .commits.nodes[0].commit.committedDate // .createdAt),"
        " .repository.nameWithOwner,"
        " (.number|tostring),"
        " (if .isDraft then \"1\" else \"0\" end),"
        " (.reviewDecision // \"\"),"
        " ([.reviewRequests.nodes[].requestedReviewer"
        "   | \"@\" + (.login // .name // \"?\")] | join(\" \")),"
        " (.headRefName // \"\"),"
        " .url, .title ] | @tsv";

    return "gh api graphql -f query=" + shellQuote(graphql) + " --jq "
           + shellQuote(jq) + " 2>&1";
}

PrItem makePrItem(const std::vector<std::string>& fields)
{
    auto item = PrItem {};
    item.ci = fields[0];
    item.pushedAt = fields[1];
    item.repo = tsvUnescape(fields[2]);
    item.number = fields[3];
    item.draft = fields[4] == "1";
    item.reviewDecision = fields[5];
    item.reviewers = tsvUnescape(fields[6]);
    item.branch = tsvUnescape(fields[7]);
    item.url = tsvUnescape(fields[8]);
    item.title = tsvUnescape(fields[9]);
    return item;
}

std::vector<PrItem> parseRows(const std::string& text)
{
    auto items = std::vector<PrItem> {};
    auto start = std::size_t {0};

    while (start <= text.size())
    {
        const auto end = text.find('\n', start);
        const auto line =
            text.substr(start, end == std::string::npos ? end : end - start);

        if (line.empty())
        {
            if (end == std::string::npos)
                break;

            start = end + 1;
            continue;
        }

        if (const auto fields = splitAtSeparator(line, '\t'); fields.size() >= prFieldCount)
            items.push_back(makePrItem(fields));

        if (end == std::string::npos)
            break;

        start = end + 1;
    }

    // ISO 8601 UTC sorts lexicographically; newest push first.
    std::stable_sort(items.begin(),
                     items.end(),
                     [](const auto& a, const auto& b)
                     { return a.pushedAt > b.pushedAt; });
    return items;
}

FilePath cachePath()
{
    return FilePath::homeDirectory() / ".cache" / "cowterm" / "pr-dashboard.tsv";
}

std::string firstLine(const std::string& text)
{
    const auto end = text.find('\n');
    return end == std::string::npos ? text : text.substr(0, end);
}

std::string lastNonEmptyLine(const std::string& text)
{
    auto last = std::string {};
    auto start = std::size_t {0};

    while (start <= text.size())
    {
        const auto end = text.find('\n', start);
        const auto line =
            text.substr(start, end == std::string::npos ? end : end - start);

        if (!line.empty())
            last = line;

        if (end == std::string::npos)
            break;

        start = end + 1;
    }

    return last;
}

bool isUnfitForPath(char c)
{
    return c == '/' || c == '\\' || c == ' ' || c == ':';
}

// Filesystem-friendly leaf for a branch, matching GitWorktree's sibling
// naming: feature/foo checks out into <repo>-feature-foo.
std::string branchLeaf(const std::string& branch)
{
    auto out = std::string {};

    for (const auto c: branch)
        out += isUnfitForPath(c) ? '-' : c;

    return out;
}

struct JumpRequest
{
    std::string repo;
    std::string number;
    std::string repoName;
    std::string branch;
    std::string cloneBase;
    std::vector<std::string> candidates;
};

// The Enter jump, given a resolved main checkout ("" -> clone first): reuse
// the worktree that already has the branch, else add a dedicated sibling
// worktree for it (fetching the PR head when the branch only exists on the
// remote), mirroring env/direnv files in like the wt script. Prints the
// directory to open as its last line.
std::string gotoScript(const std::string& main, const JumpRequest& request)
{
    const auto cloneTarget = request.cloneBase + "/" + request.repoName;
    const auto& repo = request.repo;
    const auto& branch = request.branch;
    const auto leaf = branchLeaf(branch);
    const auto& number = request.number;

    // The login shell may be zsh, whose unmatched globs abort the whole
    // script (nomatch); the env-mirror loop below globs speculatively.
    return "if [ -n \"$ZSH_VERSION\" ]; then setopt null_glob; fi\n"
           "main=" + shellQuote(main) + "\n"
           "if [ -z \"$main\" ]; then\n"
           "  main=" + shellQuote(cloneTarget) + "\n"
           "  if [ ! -e \"$main\" ]; then\n"
           "    gh repo clone " + shellQuote(repo) + " \"$main\" >/dev/null 2>&1"
           " || { echo 'clone of " + repo + " failed'; exit 1; }\n"
           "  fi\n"
           "fi\n"
           "branch=" + shellQuote(branch) + "\n"
           "wt=$(git -C \"$main\" worktree list --porcelain 2>/dev/null"
           " | awk -v want=\"refs/heads/$branch\""
           " '/^worktree /{p=substr($0,10)} /^branch /{if ($2==want){print p; exit}}')\n"
           "if [ -n \"$wt\" ]; then printf '%s\\n' \"$wt\"; exit 0; fi\n"
           "wtpath=\"$main-" + leaf + "\"\n"
           "if [ ! -e \"$wtpath\" ]; then\n"
           "  git -C \"$main\" fetch origin >/dev/null 2>&1\n"
           "  if git -C \"$main\" show-ref --verify --quiet \"refs/heads/$branch\"; then\n"
           "    git -C \"$main\" worktree add \"$wtpath\" \"$branch\" >/dev/null 2>&1\n"
           "  elif git -C \"$main\" show-ref --verify --quiet \"refs/remotes/origin/$branch\"; then\n"
           "    git -C \"$main\" worktree add \"$wtpath\" -b \"$branch\" \"origin/$branch\" >/dev/null 2>&1\n"
           "  else\n"
           "    git -C \"$main\" fetch origin \"pull/" + number + "/head\" >/dev/null 2>&1\n"
           "    git -C \"$main\" worktree add \"$wtpath\" -b \"$branch\" FETCH_HEAD >/dev/null 2>&1\n"
           "  fi\n"
           "  if [ -d \"$wtpath\" ]; then\n"
           "    for env in \"$main\"/.env \"$main\"/.env.* \"$main\"/.envrc; do\n"
           "      [ -f \"$env\" ] && ln -sf \"$env\" \"$wtpath/\" 2>/dev/null\n"
           "    done\n"
           "  fi\n"
           "fi\n"
           "if [ -d \"$wtpath\" ]; then printf '%s\\n' \"$wtpath\";"
           " else printf '%s\\n' \"$main\"; fi\n";
}

std::string getRepoName(const std::string& repo)
{
    return repo.substr(repo.rfind('/') + 1);
}

// The checkout whose origin is the PR's repo: the first one named like the
// repo wins outright, else the first match of any name.
std::string findMainCheckout(const std::vector<std::string>& candidates,
                             const std::string& wantedSlug,
                             const std::string& repoName)
{
    auto fallback = std::string {};

    for (const auto& dir: candidates)
    {
        if (!std::filesystem::exists(dir + "/.git"))
            continue;

        const auto remote = readOriginRemote(dir);

        if (remote.status != 0 || slugFromRemote(remote.text) != wantedSlug)
            continue;

        if (getRepoName(dir) == repoName)
            return dir;

        if (fallback.empty())
            fallback = dir;
    }

    return fallback;
}

std::string describeJumpFailure(const CommandOutput& output, const std::string& branch)
{
    if (output.text.empty())
        return "could not open " + branch;

    return firstLine(output.text);
}

PrJumpResult runJump(const JumpRequest& request)
{
    const auto main =
        findMainCheckout(request.candidates, lowered(request.repo), request.repoName);
    const auto output = runViaLoginShell(gotoScript(main, request));
    const auto path = lastNonEmptyLine(output.text);

    if (output.status != 0 || !std::filesystem::is_directory(path))
        return {.path = {}, .error = describeJumpFailure(output, request.branch)};

    return {.path = path, .error = {}};
}

bool hasCiPassed(const PrItem& item)
{
    return item.ci == "SUCCESS";
}

bool hasCiFailed(const PrItem& item)
{
    return item.ci == "FAILURE" || item.ci == "ERROR";
}

bool isCiRunning(const PrItem& item)
{
    return item.ci == "PENDING" || item.ci == "EXPECTED";
}

Rgb getCiColour(const PrItem& item, const Theme& theme)
{
    if (hasCiPassed(item))
        return theme.ansi[2];

    if (hasCiFailed(item))
        return theme.ansi[1];

    if (isCiRunning(item))
        return theme.ansi[3];

    return theme.ansi[8];
}

bool hasCiState(const PrItem& item)
{
    return hasCiPassed(item) || hasCiFailed(item) || isCiRunning(item);
}

std::string getCiDot(const PrItem& item)
{
    return hasCiState(item) ? "●" : "○";
}

bool isApproved(const PrItem& item)
{
    return item.reviewDecision == "APPROVED";
}

bool hasChangesRequested(const PrItem& item)
{
    return item.reviewDecision == "CHANGES_REQUESTED";
}

std::string describeReviewState(const PrItem& item)
{
    if (isApproved(item))
        return "✓ " + item.reviewers;

    if (hasChangesRequested(item))
        return "✗ " + item.reviewers;

    return item.reviewers;
}

Rgb getReviewColour(const PrItem& item, const Theme& theme)
{
    if (isApproved(item))
        return theme.ansi[2];

    if (hasChangesRequested(item))
        return theme.ansi[1];

    return theme.ansi[8];
}
} // namespace

PrDashboard::PrDashboard(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : OverlayPanel(configToUse, sessionsToUse, fontSizes)
{
}

PrDashboard::~PrDashboard()
{
    *alive = false;
}

void PrDashboard::show()
{
    query.clear();
    selected = 0;
    shown = true;
    busy.clear();
    ++jumpGeneration;

    if (allItems.empty())
        loadCache();

    applyQuery();
    buildSessionMap();
    refresh();
    repaint();
}

void PrDashboard::loadCache()
{
    const auto text = Files::readFile(cachePath());

    if (!text.empty())
        allItems = parseRows(text);
}

void PrDashboard::refresh()
{
    if (refreshing)
        return;

    refreshing = true;
    status = "refreshing…";

    runViaLoginShellAsync(
        fetchCommand(),
        [this, guard = std::weak_ptr<bool> {alive}](CommandOutput output)
        {
            if (guard.expired())
                return;

            refreshing = false;

            if (output.status != 0)
            {
                status = truncateWithEllipsis(firstLine(output.text), statusLength);

                if (status.empty())
                    status = "gh failed — is it installed and authed?";
            }
            else
            {
                allItems = parseRows(output.text);
                status = allItems.empty()
                             ? "no open PRs"
                             : std::to_string(allItems.size()) + " open";

                try
                {
                    std::filesystem::create_directories(
                        (FilePath::homeDirectory() / ".cache" / "cowterm").str());
                    Files::writeFileAtomically(
                        cachePath(),
                        {reinterpret_cast<const std::uint8_t*>(output.text.data()),
                         output.text.size()});
                }
                catch (const std::exception&)
                {
                }
            }

            applySessionMap();
            applyQuery();
            repaint();
        });
}

void PrDashboard::buildSessionMap()
{
    // Snapshot the open sessions on the main thread; resolve each project
    // dir's origin remote on a worker (git is a process spawn per session).
    auto snapshot = std::vector<std::array<std::string, 3>> {};

    for (const auto& session: sessions.all())
        if (!session->projectDir.empty())
            snapshot.push_back({session->key(), session->name, session->projectDir});

    std::thread(
        [snapshot = std::move(snapshot), guard = std::weak_ptr<bool> {alive}, this]
        {
            auto map = resolveSessionsByRepo(snapshot);

            Threads::callAsync(
                [map = std::move(map), guard, this]
                {
                    if (guard.expired())
                        return;

                    sessionsByRepo = map;
                    applySessionMap();
                    applyQuery();
                    repaint();
                });
        })
        .detach();
}

void PrDashboard::applySessionMap()
{
    for (auto& item: allItems)
    {
        const auto found = sessionsByRepo.find(lowered(item.repo));

        if (found != sessionsByRepo.end())
        {
            item.sessionKey = found->second.first;
            item.sessionName = found->second.second;
        }
        else
        {
            item.sessionKey.clear();
            item.sessionName.clear();
        }
    }
}

void PrDashboard::applyQuery()
{
    visible.clear();

    if (query.empty())
    {
        visible = allItems;
    }
    else
    {
        auto scored = std::vector<std::pair<int, const PrItem*>> {};

        for (const auto& item: allItems)
        {
            const auto haystack = item.repo + " " + item.title + " " + item.branch
                                  + " " + item.reviewers + " " + item.sessionName;

            if (auto score = fuzzyScore(query, haystack))
                scored.emplace_back(*score, &item);
        }

        std::stable_sort(scored.begin(),
                         scored.end(),
                         [](const auto& a, const auto& b)
                         { return a.first > b.first; });

        for (auto& [score, item]: scored)
            visible.push_back(*item);
    }

    selected = std::clamp(selected, 0, std::max((int) visible.size() - 1, 0));
}

const PrItem* PrDashboard::selectedItem() const
{
    if (selected < 0 || selected >= (int) visible.size())
        return nullptr;

    return &visible[(std::size_t) selected];
}

void PrDashboard::chooseSelected()
{
    if (const auto* item = selectedItem(); item != nullptr && busy.empty())
        openWorktree(*item);
}

void PrDashboard::dismiss()
{
    shown = false;
    onClosed();
}

// Candidate checkouts, scanned on the worker: sessions we already mapped to
// this repo first, then every project dir (basename matches before the rest,
// so the common case runs one git call, not fifty).
std::vector<std::string> PrDashboard::collectCandidateCheckouts(
    const PrItem& item, const std::string& repoName) const
{
    auto candidates = std::vector<std::string> {};

    if (const auto found = sessionsByRepo.find(lowered(item.repo));
        found != sessionsByRepo.end())
        candidates.push_back(found->second.first);

    auto likely = std::vector<std::string> {};
    auto rest = std::vector<std::string> {};

    for (const auto& project: scanProjects(config))
        (project.name.starts_with(repoName) ? likely : rest).push_back(project.path);

    candidates.insert(candidates.end(), likely.begin(), likely.end());
    candidates.insert(candidates.end(), rest.begin(), rest.end());
    return candidates;
}

void PrDashboard::openWorktree(const PrItem& itemRef)
{
    const auto item = itemRef;  // survives list refreshes while the job runs
    const auto repoName = getRepoName(item.repo);
    const auto branch = item.branch.empty() ? "pr-" + item.number : item.branch;

    busy = "opening " + branch + "…";
    repaint();

    auto candidates = collectCandidateCheckouts(item, repoName);
    const auto cloneBase =
        expandHome(config.searchDirs.empty() ? "~" : config.searchDirs.front());

    auto request = JumpRequest {.repo = item.repo,
                                .number = item.number,
                                .repoName = repoName,
                                .branch = branch,
                                .cloneBase = cloneBase,
                                .candidates = std::move(candidates)};

    std::thread(
        [request = std::move(request),
         generation = jumpGeneration,
         guard = std::weak_ptr<bool> {alive},
         this]
        {
            const auto result = runJump(request);

            Threads::callAsync(
                [result, generation, guard, this]
                {
                    if (!guard.expired())
                        finishJumpIfStillWanted(result, generation);
                });
        })
        .detach();
}

void PrDashboard::finishJumpIfStillWanted(const PrJumpResult& result, int generation)
{
    if (shown && generation == jumpGeneration)
        finishJump(result);
}

void PrDashboard::finishJump(const PrJumpResult& result)
{
    busy.clear();

    if (!result.error.empty())
    {
        status = truncateWithEllipsis(result.error, statusLength);
        repaint();
        return;
    }

    shown = false;
    onClosed();
    sessions.openProject(result.path);
}

void PrDashboard::switchToSession(const PrItem& item)
{
    if (item.sessionKey.empty())
        return;

    if (auto* session = sessions.find(item.sessionKey))
    {
        shown = false;
        onClosed();
        sessions.switchTo(*session);
    }
}

void PrDashboard::openInBrowser(const PrItem& item)
{
    if (item.url.empty())
        return;

    term::openInBrowser(item.url);

    dismiss();
}

void PrDashboard::copyUrl(const PrItem& item)
{
    if (!item.url.empty())
        Clipboard::copyText(item.url);
}

void PrDashboard::moveSelection(int delta)
{
    if (!visible.empty())
    {
        selected = wrapSelection(selected, delta, (int) visible.size());
        repaint();
    }
}

bool PrDashboard::keyDown(const KeyEvent& event)
{
    if (handleNavigationKey(event))
        return true;

    if (event.modifiers.control)
        handleControlShortcut(event);
    else if (event.keyCode == KeyCode::Delete || !event.modifiers.command)
        editQuery(event);

    return true;
}

bool PrDashboard::handleNavigationKey(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
        dismiss();
    else if (event.keyCode == KeyCode::Return)
        chooseSelected();
    else if (isSelectPreviousKey(event))
        moveSelection(-1);
    else if (isSelectNextKey(event))
        moveSelection(1);
    else
        return false;

    return true;
}

void PrDashboard::handleControlShortcut(const KeyEvent& event)
{
    const auto& chars = event.charactersIgnoringModifiers;

    if (chars == "r")
    {
        refresh();
        return;
    }

    const auto* item = selectedItem();

    if (item == nullptr)
        return;

    if (chars == "o")
        openInBrowser(*item);
    else if (chars == "s")
        switchToSession(*item);
    else if (chars == "y")
        copyUrl(*item);
}

void PrDashboard::editQuery(const KeyEvent& event)
{
    if (!editQueryText(query, event))
        return;

    applyQuery();
    repaint();
}

Rect PrDashboard::calculatePanelBounds() const
{
    return calculateListPanelBounds(getLocalBounds(), listMetrics, (int) visible.size());
}

int PrDashboard::findRowAt(Point pos) const
{
    return findListRowAt(calculatePanelBounds(), listMetrics, (int) visible.size(), pos);
}

void PrDashboard::mouseMove(const MouseEvent& event)
{
    if (const auto row = findRowAt(event.position); row >= 0 && row != selected)
    {
        selected = row;
        repaint();
    }
}

// Title, filter query, and the fetch state on the right.
void PrDashboard::paintHeader(UI::Graphics& g, Rect panel)
{
    const auto baseline = panel.y + headerBaseline;

    g.setColour(toColor(theme.ansi[5]));
    drawTextInFont(g, "prs", {panel.x + textLeft, baseline}, headerFont);

    g.setColour(toColor(theme.foreground));
    drawTextInFont(g, "› " + query + "▏", {panel.x + queryLeft, baseline}, headerFont);

    if (!status.empty())
    {
        const auto width = measureTextInFont(g, status, detailFont);
        g.setColour(toColor(theme.ansi[8]));
        drawTextInFont(g,
                       status,
                       {panel.right() - width - statusRightPadding, baseline},
                       detailFont);
    }

    paintHeaderDivider(g, panel, listMetrics.headerHeight);
}

void PrDashboard::paintRow(UI::Graphics& g, const PrItem& item, Rect rowArea, std::int64_t now)
{
    const auto baseline = rowArea.y + listMetrics.rowHeight * baselineFraction;

    g.setColour(toColor(getCiColour(item, theme)));
    drawTextInFont(g, getCiDot(item), {rowArea.x + ciDotLeft, baseline}, rowFont);

    // Age of the last push.
    g.setColour(toColor(theme.ansi[8]));
    drawTextInFont(g,
                   formatAge(now - epochFromIso(item.pushedAt)),
                   {rowArea.x + ageLeft, baseline},
                   detailFont);

    // Repo (name only; the slug is in the filter haystack).
    g.setColour(toColor(theme.ansi[4]));
    drawTextInFont(g,
                   truncateWithEllipsis(getRepoName(item.repo), repoNameLength),
                   {rowArea.x + repoLeft, baseline},
                   rowFont);

    // Title, flexible width between the repo column and the review block.
    const auto titleX = rowArea.x + titleLeft;
    const auto titleRight =
        rowArea.right() - sessionColumn - reviewColumn - titleRightPadding;
    const auto titleChars = (std::size_t) std::max(
        minimumTitleLength, (titleRight - titleX) / averageTitleCharacterWidth);
    const auto title = (item.draft ? "◌ " : "") + item.title;
    const auto titleAlpha = item.draft ? draftTitleAlpha : readyTitleAlpha;
    g.setColour(toColor(theme.foreground, titleAlpha));
    drawTextInFont(g, truncateWithEllipsis(title, titleChars), {titleX, baseline}, rowFont);

    paintReviewState(g, item, rowArea, baseline);
    paintOwningSession(g, item, rowArea, baseline);
}

// Decision mark plus requested reviewers.
void PrDashboard::paintReviewState(UI::Graphics& g,
                                   const PrItem& item,
                                   Rect rowArea,
                                   float baseline)
{
    const auto review = describeReviewState(item);

    if (review.empty())
        return;

    const auto text = truncateWithEllipsis(review, reviewLength);
    const auto width = measureTextInFont(g, text, detailFont);
    g.setColour(toColor(getReviewColour(item, theme)));
    drawTextInFont(
        g,
        text,
        {rowArea.right() - sessionColumn - columnRightPadding - width, baseline},
        detailFont);
}

// The session that has this repo checked out, if any.
void PrDashboard::paintOwningSession(UI::Graphics& g,
                                     const PrItem& item,
                                     Rect rowArea,
                                     float baseline)
{
    const auto hasSession = !item.sessionName.empty();
    const auto sessionText =
        hasSession ? truncateWithEllipsis(item.sessionName, sessionNameLength) : "—";
    const auto sessionWidth = measureTextInFont(g, sessionText, detailFont);
    const auto sessionColour = hasSession ? theme.ansi[6] : theme.ansi[8];
    g.setColour(toColor(sessionColour));
    drawTextInFont(g,
                   sessionText,
                   {rowArea.right() - sessionWidth - columnRightPadding, baseline},
                   detailFont);
}

// The hint line -- or the in-flight jump, which owns the footer while it runs.
void PrDashboard::paintFooter(UI::Graphics& g, Rect panel)
{
    const auto hintPosition = Point {panel.x + textLeft, panel.bottom() - hintBottomOffset};

    if (!busy.empty())
    {
        g.setColour(toColor(theme.ansi[3]));
        drawTextInFont(g, busy, hintPosition, detailFont);
        return;
    }

    g.setColour(toColor(theme.ansi[8]));
    drawTextInFont(g,
                   "enter worktree · ^o browser · ^s session · ^y copy · ^r refresh",
                   hintPosition,
                   detailFont);
}

void PrDashboard::paint(UI::Graphics& g)
{
    const auto panel = calculatePanelBounds();
    const auto now = (std::int64_t) std::time(nullptr);

    paintListBackdropAndCard(g, panel);
    paintHeader(g, panel);

    const auto rows = countVisibleListRows(listMetrics, (int) visible.size());

    for (auto i = 0; i < rows; ++i)
    {
        const auto rowArea = Rect {panel.x + rowSideInset,
                                   calculateListRowTop(panel, listMetrics, i),
                                   panel.w - 2.0f * rowSideInset,
                                   listMetrics.rowHeight - rowGap};

        if (i == selected)
            paintSelectedRowHighlight(g, rowArea);

        paintRow(g, visible[(std::size_t) i], rowArea, now);
    }

    if (visible.empty())
    {
        const auto isLoading = allItems.empty() && refreshing;
        const auto message = isLoading ? "loading PRs…" : "no matches";
        const auto emptyBaseline =
            panel.y + listMetrics.headerHeight + listMetrics.rowHeight * baselineFraction;
        g.setColour(toColor(theme.ansi[8]));
        drawTextInFont(g, message, {panel.x + textLeft, emptyBaseline}, rowFont);
    }

    paintFooter(g, panel);
}
} // namespace term
