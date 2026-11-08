#include "RudderContexts.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace term
{
namespace
{
constexpr auto panePrefix = std::string_view {"pane:"};
constexpr auto placePrefix = std::string_view {"place:"};

bool isPathInsideHome(const std::string& path, const std::string& home)
{
    if (home.empty() || !path.starts_with(home))
        return false;

    return path.size() == home.size() || path[home.size()] == '/';
}

std::string compactPath(const std::string& path, const std::string& home)
{
    if (isPathInsideHome(path, home))
        return "~" + path.substr(home.size());

    return path;
}

std::string baseName(std::string path)
{
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// A shell at its prompt says nothing the cwd doesn't. The last three are
// Windows', named as the Pty reports them (without .exe).
bool isShell(const std::string& command)
{
    constexpr auto shells = std::array<std::string_view, 9> {
        "zsh", "bash", "fish", "sh", "nu", "login",
        "pwsh", "powershell", "cmd"};

    auto name = baseName(command);

    if (name.starts_with('-'))
        name.erase(0, 1);

    for (auto shell: shells)
        if (name == shell)
            return true;

    return false;
}

// Programs animate a status glyph at the front of the title (Claude Code's
// spinner turns every second). It says nothing searchable, and keeping it
// would re-push the whole list on every frame, so a leading word without a
// single letter or digit goes.
bool isSearchableTitleCharacter(char character)
{
    const auto unsignedCharacter = static_cast<unsigned char>(character);

    if (std::isalnum(unsignedCharacter))
        return true;

    return character == '~' || character == '/' || character == '.';
}

std::string withoutStatusGlyph(const std::string& title)
{
    const auto space = title.find(' ');

    if (space == std::string::npos || space == 0)
        return title;

    const auto firstWord = std::string_view {title}.substr(0, space);

    if (std::ranges::any_of(firstWord, isSearchableTitleCharacter))
        return title;

    const auto rest = title.find_first_not_of(' ', space);
    return rest == std::string::npos ? std::string {} : title.substr(rest);
}

constexpr auto versionSeparatorCount = 2;

bool canStartNextVersionPart(int digits, int parts)
{
    return digits > 0 && parts < versionSeparatorCount;
}

// "2.1.300": digits.digits.digits and nothing else.
bool isVersion(std::string_view text)
{
    auto parts = 0;
    auto digits = 0;

    for (auto c: text)
    {
        if (std::isdigit((unsigned char) c))
            ++digits;
        else if (c == '.' && canStartNextVersionPart(digits, parts))
            ++parts, digits = 0;
        else
            return false;
    }

    return parts == versionSeparatorCount && digits > 0;
}

// Claude Code's native install runs as ~/.local/share/claude/versions/2.1.300,
// so its process name is its version: that says nothing, and would take the
// front of the title. Same for a title that leads with a bare version and a
// separator ("2.1.300 · Task").
std::string commandName(const std::string& command)
{
    auto name = baseName(command);
    return isVersion(name) ? std::string {} : name;
}

std::string withoutLeadingVersion(const std::string& title)
{
    const auto space = title.find(' ');

    if (space == std::string::npos || !isVersion(std::string_view {title}.substr(0, space)))
        return title;

    constexpr auto separator = std::string_view {"·"};
    auto rest = title.find_first_not_of(' ', space);

    if (rest != std::string::npos && title.compare(rest, separator.size(), separator) == 0)
        rest = title.find_first_not_of(' ', rest + separator.size());

    return rest == std::string::npos ? title : title.substr(rest);
}

// "<branch> · <command> · <title>", leaving out whatever is empty or already
// said, so a search for a command finds the pane. In a repo the branch leads:
// it names the work, where the directory (a worktree's, often) doesn't. A
// shell titled with its cwd says nothing the subtitle doesn't, so the branch
// stands alone there; outside a repo, the cwd. The directory itself goes to
// Rudder as the context's path, which it matches on.
std::string prependWithSeparator(const std::string& lead, const std::string& title)
{
    if (title.empty())
        return lead;

    return lead + " · " + title;
}

bool isShellTitledWithItsDirectory(const RudderPane& pane,
                                   const std::string& title,
                                   const std::string& cwd)
{
    if (pane.branch.empty() || !isShell(pane.command))
        return false;

    return title == cwd || title == pane.cwd;
}

bool shouldPrependCommand(const std::string& command, const std::string& title)
{
    if (command.empty() || isShell(command))
        return false;

    return title.find(command) == std::string::npos;
}

bool shouldPrependBranch(const std::string& branch, const std::string& title)
{
    if (branch.empty() || title == branch)
        return false;

    return !title.starts_with(branch + " · ");
}

std::string paneTitle(const RudderPane& pane, const std::string& home)
{
    const auto cwd = compactPath(pane.cwd, home);
    auto title = withoutLeadingVersion(withoutStatusGlyph(pane.title));
    const auto command = commandName(pane.command);

    if (isShellTitledWithItsDirectory(pane, title, cwd))
        title.clear();

    if (shouldPrependCommand(command, title))
        title = prependWithSeparator(command, title);

    if (shouldPrependBranch(pane.branch, title))
        title = prependWithSeparator(pane.branch, title);

    if (title.empty())
        return cwd;

    return title;
}

// "<cwd> · Tab <n> · pane <m> · <tab name>": where the pane is, the cwd first
// unless the title already shows it.
struct PanePlacement
{
    std::size_t tab = 0;
    std::size_t index = 0;
    const std::string& tabName;
};

bool doesTitleMention(const std::string& title, const std::string& text)
{
    return title.find(text) != std::string::npos;
}

std::string paneSubtitle(const RudderPane& pane,
                         const std::string& title,
                         const PanePlacement& placement,
                         const std::string& home)
{
    auto where = "Tab " + std::to_string(placement.tab + 1) + " · pane "
                 + std::to_string(placement.index + 1) + " · " + placement.tabName;
    const auto cwd = compactPath(pane.cwd, home);

    if (cwd.empty())
        return where;

    if (doesTitleMention(title, cwd) || doesTitleMention(title, pane.cwd))
        return where;

    return cwd + " · " + where;
}

rudder::protocol::Context createPaneContext(const RudderPane& pane,
                                           const PanePlacement& placement,
                                           const std::string& home)
{
    auto context = rudder::protocol::Context {};
    context.id = paneContextId(pane.shellId);
    context.title = paneTitle(pane, home);
    context.subtitle = paneSubtitle(pane, context.title, placement, home);
    context.path = pane.cwd;
    context.branch = pane.branch;
    context.repo = pane.repo;
    context.focused = pane.focused;
    return context;
}

rudder::protocol::Context createPlaceContext(const RudderPlace& place,
                                            const std::string& home)
{
    auto context = rudder::protocol::Context {};
    context.id = std::string {placePrefix} + place.path;
    context.title = baseName(place.path);
    context.subtitle = compactPath(place.path, home);
    context.path = place.path;
    context.branch = place.branch;
    context.repo = place.repo;
    context.open = false;
    return context;
}

struct CollectedContexts
{
    std::vector<rudder::protocol::Context> contexts;

    // Tab keys and pane directories already listed, so no place repeats one.
    std::unordered_set<std::string> covered;
};

void appendTabContexts(const RudderTab& session,
                       std::size_t tab,
                       const std::string& home,
                       CollectedContexts& collected)
{
    collected.covered.insert(session.key);

    for (std::size_t index = 0; index < session.panes.size(); ++index)
    {
        const auto& pane = session.panes[index];
        collected.covered.insert(pane.cwd);
        collected.contexts.push_back(
            createPaneContext(pane, PanePlacement {tab, index, session.name}, home));
    }
}
} // namespace

std::vector<rudder::protocol::Context>
    rudderContexts(const std::vector<RudderTab>& tabs,
                   const std::vector<RudderPlace>& places,
                   const std::string& home)
{
    auto collected = CollectedContexts {};

    for (std::size_t tab = 0; tab < tabs.size(); ++tab)
        appendTabContexts(tabs[tab], tab, home, collected);

    for (const auto& place: places)
    {
        if (!collected.covered.contains(place.path))
            collected.contexts.push_back(createPlaceContext(place, home));
    }

    return collected.contexts;
}

std::optional<RudderTarget> parseRudderContextId(const std::string& id)
{
    if (id.starts_with(panePrefix) && id.size() > panePrefix.size())
        return RudderTarget {RudderTarget::Kind::Pane, id.substr(panePrefix.size())};

    if (id.starts_with(placePrefix) && id.size() > placePrefix.size())
        return RudderTarget {RudderTarget::Kind::Place,
                             id.substr(placePrefix.size())};

    return std::nullopt;
}
std::string paneContextId(const std::string& shellId)
{
    return std::string {panePrefix} + shellId;
}
} // namespace term
