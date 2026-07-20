#include "PrDashboard.h"

#include "Command.h"
#include "FuzzyMatch.h"
#include "Projects.h"

#include <eacp/Core/App/Clipboard.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Files.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <thread>

namespace term
{
using namespace eacp;
using Graphics::Color;
using Graphics::Context;
using Graphics::KeyEvent;
using Graphics::MouseEvent;
using Graphics::Point;
using Graphics::Rect;
namespace KeyCode = Graphics::KeyCode;

namespace
{
constexpr float panelWidth = 960.0f;
constexpr float rowHeight = 32.0f;
constexpr float headerHeight = 46.0f;
constexpr float hintHeight = 24.0f;
constexpr int maxRows = 14;

// Right-hand columns: the owning session, and review state before it.
constexpr float sessionColumn = 130.0f;
constexpr float reviewColumn = 150.0f;

std::string truncated(const std::string& text, std::size_t max)
{
    if (text.size() <= max)
        return text;

    return text.substr(0, max - 1) + "…";
}

std::string lowered(std::string text)
{
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    return text;
}

// "owner/repo" from any git remote URL form (https, ssh://, scp-like),
// lowercased, ".git" stripped. Empty when it doesn't look like a remote.
std::string slugFromRemote(std::string url)
{
    while (!url.empty() && (url.back() == '\n' || url.back() == '\r' || url.back() == ' '))
        url.pop_back();

    if (const auto scheme = url.find("://"); scheme != std::string::npos)
    {
        url = url.substr(scheme + 3);

        if (const auto slash = url.find('/'); slash != std::string::npos)
            url = url.substr(slash + 1);
        else
            return {};
    }
    else if (const auto colon = url.find(':');
             colon != std::string::npos && url.find('/') > colon)
    {
        // scp-like: git@github.com:owner/repo.git
        url = url.substr(colon + 1);
    }

    while (!url.empty() && url.front() == '/')
        url.erase(url.begin());

    if (url.ends_with(".git"))
        url = url.substr(0, url.size() - 4);

    return url.find('/') != std::string::npos ? lowered(url) : std::string {};
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

    if (seconds >= 14 * 86400)
        return std::to_string(seconds / (7 * 86400)) + "w";

    if (seconds >= 86400)
        return std::to_string(seconds / 86400) + "d";

    if (seconds >= 3600)
        return std::to_string(seconds / 3600) + "h";

    return std::to_string(seconds / 60) + "m";
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

std::vector<std::string> splitTabs(const std::string& line)
{
    auto fields = std::vector<std::string> {};
    auto start = std::size_t {0};

    while (true)
    {
        const auto tab = line.find('\t', start);

        if (tab == std::string::npos)
        {
            fields.push_back(line.substr(start));
            return fields;
        }

        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
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

        const auto fields = splitTabs(line);

        if (fields.size() >= 10)
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
            items.push_back(std::move(item));
        }

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

// Filesystem-friendly leaf for a branch, matching GitWorktree's sibling
// naming: feature/foo checks out into <repo>-feature-foo.
std::string branchLeaf(const std::string& branch)
{
    auto out = std::string {};

    for (const auto c: branch)
        out += (c == '/' || c == '\\' || c == ' ' || c == ':') ? '-' : c;

    return out;
}

// The Enter jump, given a resolved main checkout ("" -> clone first): reuse
// the worktree that already has the branch, else add a dedicated sibling
// worktree for it (fetching the PR head when the branch only exists on the
// remote), mirroring env/direnv files in like the wt script. Prints the
// directory to open as its last line.
std::string gotoScript(const std::string& main,
                       const std::string& cloneTarget,
                       const std::string& repo,
                       const std::string& branch,
                       const std::string& leaf,
                       const std::string& number)
{
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
} // namespace

PrDashboard::PrDashboard(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : config(configToUse)
    , sessions(sessionsToUse)
    , theme(themeByName(configToUse.theme))
    , queryFont({config.font, 16.0f})
    , rowFont({config.font, 14.0f})
    , detailFont({config.font, 12.0f})
{
    setHandlesMouseEvents(true);
    setGrabsFocusOnMouseDown(true);
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
                status = truncated(firstLine(output.text), 60);

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
            auto map = std::map<std::string, std::pair<std::string, std::string>> {};

            for (const auto& [key, name, dir]: snapshot)
            {
                const auto remote = runViaLoginShell(
                    "git -C " + shellQuote(dir) + " remote get-url origin 2>/dev/null");

                if (remote.status != 0)
                    continue;

                const auto slug = slugFromRemote(remote.text);

                if (!slug.empty() && map.find(slug) == map.end())
                    map[slug] = {key, name};
            }

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

void PrDashboard::choose()
{
    if (const auto* item = selectedItem(); item != nullptr && busy.empty())
        openWorktree(*item);
}

void PrDashboard::cancel()
{
    shown = false;
    onClosed();
}

void PrDashboard::openWorktree(const PrItem& itemRef)
{
    const auto item = itemRef;  // survives list refreshes while the job runs
    const auto repoName = item.repo.substr(item.repo.rfind('/') + 1);
    const auto branch =
        item.branch.empty() ? "pr-" + item.number : item.branch;

    busy = "opening " + branch + "…";
    repaint();

    // Candidate checkouts, scanned on the worker: sessions we already mapped
    // to this repo first, then every project dir (basename matches before the
    // rest, so the common case runs one git call, not fifty).
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

    const auto cloneBase =
        expandHome(config.searchDirs.empty() ? "~" : config.searchDirs.front());

    std::thread(
        [item, repoName, branch, candidates = std::move(candidates), cloneBase,
         generation = jumpGeneration, guard = std::weak_ptr<bool> {alive}, this]
        {
            const auto want = lowered(item.repo);
            auto main = std::string {};
            auto fallback = std::string {};

            for (const auto& dir: candidates)
            {
                if (!std::filesystem::exists(dir + "/.git"))
                    continue;

                const auto remote = runViaLoginShell(
                    "git -C " + shellQuote(dir) + " remote get-url origin 2>/dev/null");

                if (remote.status != 0 || slugFromRemote(remote.text) != want)
                    continue;

                if (dir.substr(dir.rfind('/') + 1) == repoName)
                {
                    main = dir;
                    break;
                }

                if (fallback.empty())
                    fallback = dir;
            }

            if (main.empty())
                main = fallback;

            const auto output = runViaLoginShell(gotoScript(main,
                                                            cloneBase + "/" + repoName,
                                                            item.repo,
                                                            branch,
                                                            branchLeaf(branch),
                                                            item.number));
            auto path = lastNonEmptyLine(output.text);

            if (output.status != 0 || !std::filesystem::is_directory(path))
                path.clear();

            const auto error = path.empty()
                                   ? (output.text.empty() ? "could not open " + branch
                                                          : firstLine(output.text))
                                   : std::string {};

            Threads::callAsync(
                [path, error, generation, guard, this]
                {
                    if (guard.expired() || !shown || generation != jumpGeneration)
                        return;

                    busy.clear();

                    if (!error.empty())
                    {
                        status = truncated(error, 60);
                        repaint();
                        return;
                    }

                    shown = false;
                    onClosed();
                    sessions.openProject(path);
                });
        })
        .detach();
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

#if defined(__APPLE__)
    runViaLoginShellAsync("open " + shellQuote(item.url), [](CommandOutput) {});
#elif defined(_WIN32)
    runViaLoginShellAsync("start \"\" " + shellQuote(item.url), [](CommandOutput) {});
#else
    runViaLoginShellAsync("xdg-open " + shellQuote(item.url), [](CommandOutput) {});
#endif

    cancel();
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
        selected = (selected + delta + (int) visible.size()) % (int) visible.size();
        repaint();
    }
}

void PrDashboard::popQueryChar()
{
    while (!query.empty())
    {
        const auto last = (unsigned char) query.back();
        query.pop_back();

        if ((last & 0xc0) != 0x80)
            break;
    }
}

void PrDashboard::keyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        cancel();
        return;
    }

    if (event.keyCode == KeyCode::Return)
    {
        choose();
        return;
    }

    if (event.keyCode == KeyCode::UpArrow
        || (event.modifiers.control && event.charactersIgnoringModifiers == "p"))
    {
        moveSelection(-1);
        return;
    }

    if (event.keyCode == KeyCode::DownArrow
        || (event.modifiers.control && event.charactersIgnoringModifiers == "n"))
    {
        moveSelection(1);
        return;
    }

    if (event.modifiers.control)
    {
        const auto& chars = event.charactersIgnoringModifiers;
        const auto* item = selectedItem();

        if (chars == "r")
            refresh();
        else if (chars == "o" && item != nullptr)
            openInBrowser(*item);
        else if (chars == "s" && item != nullptr)
            switchToSession(*item);
        else if (chars == "y" && item != nullptr)
            copyUrl(*item);

        return;
    }

    if (event.keyCode == KeyCode::Delete)
    {
        popQueryChar();
        applyQuery();
        repaint();
        return;
    }

    if (event.modifiers.command)
        return;

    const auto& text = event.characters;

    if (!text.empty() && (unsigned char) text[0] >= 0x20 && text[0] != 0x7f)
    {
        query += text;
        selected = 0;
        applyQuery();
        repaint();
    }
}

Rect PrDashboard::panelBounds() const
{
    const auto bounds = getLocalBounds();
    const auto width = std::min(panelWidth, bounds.w - 60.0f);
    const auto rows = std::min((int) visible.size(), maxRows);
    const auto height = headerHeight + (float) std::max(rows, 1) * rowHeight
                        + hintHeight + 12.0f;

    return {
        (bounds.w - width) / 2.0f, std::max(bounds.h * 0.12f, 20.0f), width, height};
}

int PrDashboard::rowAt(Point pos) const
{
    const auto panel = panelBounds();
    const auto y = pos.y - (panel.y + headerHeight + 4.0f);

    if (pos.x < panel.x || pos.x > panel.right() || y < 0)
        return -1;

    const auto row = (int) (y / rowHeight);
    return row < std::min((int) visible.size(), maxRows) ? row : -1;
}

void PrDashboard::mouseMoved(const MouseEvent& event)
{
    if (const auto row = rowAt(event.pos); row >= 0 && row != selected)
    {
        selected = row;
        repaint();
    }
}

void PrDashboard::mouseDown(const MouseEvent& event)
{
    const auto row = rowAt(event.pos);

    if (row >= 0)
    {
        selected = row;
        choose();
        return;
    }

    if (!panelBounds().contains(event.pos))
        cancel();
}

void PrDashboard::paint(Context& context)
{
    const auto panel = panelBounds();
    const auto now = (std::int64_t) std::time(nullptr);

    context.setColor(Color::black(0.38f));
    context.fillRect(getLocalBounds());

    context.setColor(toColor(theme.background).brighter(0.04f));
    context.fillRoundedRect(panel, 12.0f);

    context.setColor(toColor(theme.selection, 0.8f));
    context.setLineWidth(1.0f);
    context.strokeRect(panel);

    // Header: title, filter query, and the fetch state on the right.
    context.setColor(toColor(theme.ansi[5]));
    context.drawText("prs", {panel.x + 18.0f, panel.y + 30.0f}, queryFont);

    const auto queryText = "› " + query + "▏";
    context.setColor(toColor(theme.foreground));
    context.drawText(queryText, {panel.x + 66.0f, panel.y + 30.0f}, queryFont);

    if (!status.empty())
    {
        const auto width = Graphics::TextMetrics::measureWidth(status, detailFont);
        context.setColor(toColor(theme.ansi[8]));
        context.drawText(
            status, {panel.right() - width - 16.0f, panel.y + 30.0f}, detailFont);
    }

    context.setColor(toColor(theme.selection));
    context.drawLine({panel.x + 12.0f, panel.y + headerHeight - 2.0f},
                     {panel.right() - 12.0f, panel.y + headerHeight - 2.0f});

    const auto rows = std::min((int) visible.size(), maxRows);

    for (auto i = 0; i < rows; ++i)
    {
        const auto& item = visible[(std::size_t) i];
        const auto y = panel.y + headerHeight + 4.0f + (float) i * rowHeight;
        const auto rowRect =
            Rect {panel.x + 8.0f, y, panel.w - 16.0f, rowHeight - 2.0f};

        if (i == selected)
        {
            context.setColor(toColor(theme.selection, 0.85f));
            context.fillRoundedRect(rowRect, 6.0f);
        }

        const auto baseline = y + rowHeight * 0.62f;

        // CI state dot.
        const auto success = item.ci == "SUCCESS";
        const auto failed = item.ci == "FAILURE" || item.ci == "ERROR";
        const auto running = item.ci == "PENDING" || item.ci == "EXPECTED";
        const auto dotColor = success   ? toColor(theme.ansi[2])
                              : failed  ? toColor(theme.ansi[1])
                              : running ? toColor(theme.ansi[3])
                                        : toColor(theme.ansi[8]);
        context.setColor(dotColor);
        context.drawText(success || failed || running ? "●" : "○",
                         {rowRect.x + 12.0f, baseline},
                         rowFont);

        // Age of the last push.
        context.setColor(toColor(theme.ansi[8]));
        context.drawText(formatAge(now - epochFromIso(item.pushedAt)),
                         {rowRect.x + 34.0f, baseline},
                         detailFont);

        // Repo (name only; the slug is in the filter haystack).
        const auto repoName = item.repo.substr(item.repo.rfind('/') + 1);
        context.setColor(toColor(theme.ansi[4]));
        context.drawText(
            truncated(repoName, 18), {rowRect.x + 76.0f, baseline}, rowFont);

        // Title, flexible width between the repo column and the review block.
        const auto titleX = rowRect.x + 226.0f;
        const auto titleRight =
            rowRect.right() - sessionColumn - reviewColumn - 24.0f;
        const auto titleChars =
            (std::size_t) std::max(8.0f, (titleRight - titleX) / 7.2f);
        const auto title = (item.draft ? "◌ " : "") + item.title;
        context.setColor(toColor(theme.foreground, item.draft ? 0.6f : 1.0f));
        context.drawText(truncated(title, titleChars), {titleX, baseline}, rowFont);

        // Review state: decision mark plus requested reviewers.
        const auto approved = item.reviewDecision == "APPROVED";
        const auto changes = item.reviewDecision == "CHANGES_REQUESTED";
        const auto review = std::string {approved  ? "✓ "
                                         : changes ? "✗ "
                                                   : ""}
                            + item.reviewers;

        if (!review.empty())
        {
            const auto text = truncated(review, 20);
            const auto width = Graphics::TextMetrics::measureWidth(text, detailFont);
            context.setColor(approved  ? toColor(theme.ansi[2])
                             : changes ? toColor(theme.ansi[1])
                                       : toColor(theme.ansi[8]));
            context.drawText(
                text,
                {rowRect.right() - sessionColumn - 12.0f - width, baseline},
                detailFont);
        }

        // The session that has this repo checked out, if any.
        const auto sessionText =
            item.sessionName.empty() ? "—" : truncated(item.sessionName, 16);
        const auto sessionWidth =
            Graphics::TextMetrics::measureWidth(sessionText, detailFont);
        context.setColor(item.sessionName.empty() ? toColor(theme.ansi[8])
                                                  : toColor(theme.ansi[6]));
        context.drawText(sessionText,
                         {rowRect.right() - sessionWidth - 12.0f, baseline},
                         detailFont);
    }

    if (visible.empty())
    {
        context.setColor(toColor(theme.ansi[8]));
        context.drawText(
            allItems.empty() && refreshing ? "loading PRs…" : "no matches",
            {panel.x + 18.0f, panel.y + headerHeight + rowHeight * 0.62f},
            rowFont);
    }

    // Hint line — or the in-flight jump, which owns the footer while it runs.
    const auto hintY = panel.bottom() - 10.0f;

    if (!busy.empty())
    {
        context.setColor(toColor(theme.ansi[3]));
        context.drawText(busy, {panel.x + 18.0f, hintY}, detailFont);
    }
    else
    {
        context.setColor(toColor(theme.ansi[8]));
        context.drawText(
            "enter worktree · ^o browser · ^s session · ^y copy · ^r refresh",
            {panel.x + 18.0f, hintY},
            detailFont);
    }
}
} // namespace term
