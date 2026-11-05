#include "BoardEnvironment.h"

#include <ClaudeExecutable.h>
#include <ExecutableLookup.h>
#include <Sessions.h>

#include <eacp/Core/Utils/Environment.h>

namespace term
{
using eacp::getEnvValue;

std::optional<BoardQuery> makeClaudeQueryFromEnvironment()
{
    auto search = claudecli::ExecutableSearch {};
    search.pathDirectories = executablelookup::splitSearchPath(getEnvValue("PATH"));
    search.homeDirectory = getEnvValue("HOME");
    auto executable = claudecli::resolveClaudeExecutable(search);

    if (!executable)
        return std::nullopt;

    auto query = BoardQuery {};
    query.launch.executable = *executable;
    query.launch.removedEnvironment = claudecli::listNestedSessionVariables(
        getEnvValue(std::string {claudecli::agentVariableName}));
    query.configDirectory = claudecli::resolveClaudeConfigDirectory(
        getEnvValue("CLAUDE_CONFIG_DIR"), search.homeDirectory);

    return query;
}

namespace
{
std::string findFirstExecutable(std::string_view name, const std::vector<std::string>& directories)
{
    auto found = executablelookup::findExecutables(name, directories, {});

    if (found.empty())
        return std::string {name};

    return found.front();
}
} // namespace

StartAgentSettings makeStartAgentSettingsFromEnvironment()
{
    auto home = getEnvValue("HOME");
    auto directories = executablelookup::splitSearchPath(getEnvValue("PATH"));

    for (auto fallback: {"/etc/profiles/per-user/" + getEnvValue("USER") + "/bin",
                         home + "/.nix-profile/bin",
                         std::string {"/run/current-system/sw/bin"},
                         std::string {"/opt/homebrew/bin"},
                         std::string {"/usr/local/bin"},
                         std::string {"/usr/bin"}})
        directories.push_back(fallback);

    auto settings = StartAgentSettings {};
    settings.projectsDirectory = home + "/projects";
    settings.gitExecutable = findFirstExecutable("git", directories);
    settings.ghExecutable = findFirstExecutable("gh", directories);
    return settings;
}
} // namespace term
