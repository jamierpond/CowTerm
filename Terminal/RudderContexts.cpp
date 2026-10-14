#include "RudderContexts.h"

#include <array>
#include <cctype>
#include <unordered_set>

namespace term
{
namespace
{
constexpr auto panePrefix = std::string_view {"pane:"};
constexpr auto placePrefix = std::string_view {"place:"};

std::string compactPath(const std::string& path, const std::string& home)
{
    if (!home.empty() && path.starts_with(home)
        && (path.size() == home.size() || path[home.size()] == '/'))
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
std::string withoutStatusGlyph(const std::string& title)
{
    const auto space = title.find(' ');

    if (space == std::string::npos || space == 0)
        return title;

    for (std::size_t i = 0; i < space; ++i)
    {
        const auto c = (unsigned char) title[i];

        if (std::isalnum(c) || c == '~' || c == '/' || c == '.')
            return title;
    }

    const auto rest = title.find_first_not_of(' ', space);
    return rest == std::string::npos ? std::string {} : title.substr(rest);
}

// "<command> · <title>", leaving out whatever is empty or already said, so a
// search for a command finds the pane; the cwd when there's nothing else. The
// directory itself goes to Rudder as the context's path, which it matches on.
std::string paneTitle(const RudderPane& pane, const std::string& home)
{
    auto title = withoutStatusGlyph(pane.title);

    if (!pane.command.empty() && !isShell(pane.command)
        && title.find(baseName(pane.command)) == std::string::npos)
        title = title.empty() ? baseName(pane.command)
                              : baseName(pane.command) + " · " + title;

    return title.empty() ? compactPath(pane.cwd, home) : title;
}

// "<cwd> · Tab <n> · pane <m> · <tab name>": where the pane is, the cwd first
// unless the title already shows it.
std::string paneSubtitle(const RudderPane& pane,
                         const std::string& title,
                         std::size_t tab,
                         std::size_t index,
                         const std::string& tabName,
                         const std::string& home)
{
    auto where = "Tab " + std::to_string(tab + 1) + " · pane " + std::to_string(index + 1)
                 + " · " + tabName;
    const auto cwd = compactPath(pane.cwd, home);

    if (cwd.empty() || title.find(cwd) != std::string::npos
        || title.find(pane.cwd) != std::string::npos)
        return where;

    return cwd + " · " + where;
}
} // namespace

std::vector<rudder::protocol::Context>
    rudderContexts(const std::vector<RudderTab>& tabs,
                   const std::vector<std::string>& placePaths,
                   const std::string& home)
{
    auto contexts = std::vector<rudder::protocol::Context> {};
    auto covered = std::unordered_set<std::string> {};

    for (std::size_t tab = 0; tab < tabs.size(); ++tab)
    {
        const auto& session = tabs[tab];
        covered.insert(session.key);

        for (std::size_t index = 0; index < session.panes.size(); ++index)
        {
            const auto& pane = session.panes[index];
            covered.insert(pane.cwd);

            auto context = rudder::protocol::Context {};
            context.id = std::string {panePrefix} + pane.shellId;
            context.title = paneTitle(pane, home);
            context.subtitle =
                paneSubtitle(pane, context.title, tab, index, session.name, home);
            context.path = pane.cwd;
            context.focused = pane.focused;
            contexts.push_back(std::move(context));
        }
    }

    for (const auto& path: placePaths)
    {
        if (covered.contains(path))
            continue;

        auto context = rudder::protocol::Context {};
        context.id = std::string {placePrefix} + path;
        context.title = baseName(path);
        context.subtitle = compactPath(path, home);
        context.path = path;
        context.open = false;
        contexts.push_back(std::move(context));
    }

    return contexts;
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
} // namespace term
