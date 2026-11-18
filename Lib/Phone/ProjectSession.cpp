#include "ProjectSession.h"

#include "Multiplexer/RemoteLocation.h"

#include <algorithm>
#include <cstdio>

namespace term::phone
{
namespace
{
bool isFolderInScan(const std::string& host, const std::vector<ProjectDir>& lastScan, const std::string& folder)
{
    const auto projectDir = remote::makeRemoteProjectDir(host, folder);
    return std::ranges::any_of(lastScan, [&](const ProjectDir& project) { return project.path == projectDir; });
}
} // namespace

std::optional<ProjectSession> makeProjectSession(const std::string& host,
                                                 const std::vector<ProjectDir>& lastScan,
                                                 const std::string& folder,
                                                 std::uint64_t randomNumber)
{
    if (!isFolderInScan(host, lastScan, folder))
        return std::nullopt;

    const auto sessionName = getSessionNameForProject(remote::makeRemoteProjectDir(host, folder));
    return ProjectSession {host, makePaneIdFromRandomNumber(randomNumber), folder, sessionName};
}

std::string makePaneIdFromRandomNumber(std::uint64_t randomNumber)
{
    constexpr auto hexDigitsWithTerminator = 17;
    char digits[hexDigitsWithTerminator];
    const auto randomNumberForPrinting = static_cast<unsigned long long>(randomNumber);
    std::snprintf(digits, sizeof(digits), "%016llx", randomNumberForPrinting);
    return digits;
}

ProjectShell::ProjectShell(std::unique_ptr<Shell> innerToUse, ProjectSession sessionToUse)
    : inner(std::move(innerToUse))
    , session(std::move(sessionToUse))
{
}

bool ProjectShell::isAskingForLoginShellInProjectFolder(const PtyOptions& options) const
{
    const auto isInProjectFolder = options.workingDirectory.empty() || options.workingDirectory == session.folder;
    return options.command.empty() && isInProjectFolder;
}

bool ProjectShell::start(const PtyOptions& options,
                         std::function<void(std::string)> onOutput,
                         std::function<void()> onExit)
{
    if (!isAskingForLoginShellInProjectFolder(options))
        return false;

    auto inProjectFolder = options;
    inProjectFolder.workingDirectory = session.folder;
    inner->onRestoreState = onRestoreState;
    inner->onStatusChanged = onStatusChanged;
    inner->onGridSetElsewhere = onGridSetElsewhere;
    inner->onCommandNotInstalled = onCommandNotInstalled;
    if (!inner->start(inProjectFolder, std::move(onOutput), std::move(onExit)))
        return false;

    onLoginShellStarted();
    return true;
}
} // namespace term::phone
