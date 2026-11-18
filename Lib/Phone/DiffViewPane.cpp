#include "DiffViewPane.h"

#include "ProjectSession.h"

#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <Miro/Reflect.h>

#include <algorithm>

namespace term::phone
{
namespace
{
constexpr auto diffViewPaneIdPrefix = "phone-dv-";

// What dv prints, and exits, when its folder is in no git repository.
constexpr auto diffViewOutsideARepository = "not inside a git work tree";

struct TemporaryPaneEntry
{
    std::string host;
    std::string paneId;

    MIRO_REFLECT(host, paneId)
};

struct TemporaryPanesFile
{
    std::vector<TemporaryPaneEntry> panes;

    MIRO_REFLECT(panes)
};

bool saveTemporaryPanes(const std::filesystem::path& path, const std::vector<TemporaryPane>& panes)
{
    auto file = TemporaryPanesFile {};

    for (const auto& pane: panes)
        file.panes.push_back({pane.host, pane.paneId});

    auto ignoredError = std::error_code {};
    std::filesystem::create_directories(path.parent_path(), ignoredError);

    try
    {
        eacp::Files::writeFileAtomically(eacp::FilePath {path.string()}, Miro::toJSONString(file));
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// The screen's text has one line per row, so a long message is broken where
// the row ends, mid-word.
std::string joinScreenRows(std::string text)
{
    std::erase(text, '\n');
    return text;
}

std::string findLastNonEmptyLine(const std::string& text)
{
    auto end = text.find_last_not_of(" \n");

    if (end == std::string::npos)
        return {};

    const auto lineStart = text.rfind('\n', end);
    const auto start = lineStart == std::string::npos ? 0 : lineStart + 1;
    return text.substr(start, end + 1 - start);
}
} // namespace

std::optional<DiffViewPane> makeDiffViewPane(const std::string& host,
                                             const std::string& shownPaneFolder,
                                             std::uint64_t randomNumber)
{
    if (shownPaneFolder.empty())
        return std::nullopt;

    return DiffViewPane {host, makeDiffViewPaneId(randomNumber), shownPaneFolder};
}

std::string makeDiffViewPaneId(std::uint64_t randomNumber)
{
    return diffViewPaneIdPrefix + makePaneIdFromRandomNumber(randomNumber);
}

bool isDiffViewPaneId(const std::string& paneId)
{
    return paneId.starts_with(diffViewPaneIdPrefix);
}

std::vector<std::string> removeDiffViewPaneIds(std::vector<std::string> paneIds)
{
    std::erase_if(paneIds, isDiffViewPaneId);
    return paneIds;
}

std::string describeDiffViewNotInstalled(const MissingCommand& missing, const std::string& hostLabel)
{
    return describeCommandNotInstalled(missing, hostLabel);
}

std::string describeDiffViewFailure(const std::string& textLeftOnScreen, const std::string& folder)
{
    if (joinScreenRows(textLeftOnScreen).find(diffViewOutsideARepository) != std::string::npos)
        return folder + " is not in a git repository.";

    const auto lastLine = findLastNonEmptyLine(textLeftOnScreen);

    if (lastLine.empty())
        return {};

    return std::string {diffViewCommand} + " stopped: " + lastLine;
}

bool addTemporaryPane(const std::filesystem::path& path, const TemporaryPane& pane)
{
    auto panes = loadTemporaryPanes(path);
    panes.push_back(pane);
    return saveTemporaryPanes(path, panes);
}

void removeTemporaryPane(const std::filesystem::path& path, const TemporaryPane& pane)
{
    auto panes = loadTemporaryPanes(path);
    std::erase(panes, pane);

    if (!panes.empty())
    {
        saveTemporaryPanes(path, panes);
        return;
    }

    auto ignoredError = std::error_code {};
    std::filesystem::remove(path, ignoredError);
}

std::vector<TemporaryPane> loadTemporaryPanes(const std::filesystem::path& path)
{
    const auto text = eacp::Files::readFile(eacp::FilePath {path.string()});

    if (text.empty())
        return {};

    try
    {
        auto file = TemporaryPanesFile {};
        Miro::fromJSONString(file, text);
        auto panes = std::vector<TemporaryPane> {};

        // Files from before hosts were named without their user hold "jamiepond@macprorack".
        for (const auto& entry: file.panes)
            panes.push_back({remote::getHostLabel(entry.host), entry.paneId});

        return panes;
    }
    catch (const std::exception&)
    {
        return {};
    }
}

DiffViewShell::DiffViewShell(std::unique_ptr<Shell> innerToUse, DiffViewPane paneToUse)
    : inner(std::move(innerToUse))
    , pane(std::move(paneToUse))
{
}

bool DiffViewShell::isAskingForDiffViewInItsFolder(const PtyOptions& options) const
{
    return options.command == diffViewCommand && options.workingDirectory == pane.workingDirectory;
}

bool DiffViewShell::start(const PtyOptions& options,
                          std::function<void(std::string)> onOutput,
                          std::function<void()> onExit)
{
    if (!isAskingForDiffViewInItsFolder(options))
        return false;

    inner->onRestoreState = onRestoreState;
    inner->onStatusChanged = onStatusChanged;
    inner->onGridSetElsewhere = onGridSetElsewhere;
    inner->onCommandNotInstalled = onCommandNotInstalled;
    return inner->start(options, std::move(onOutput), std::move(onExit));
}
} // namespace term::phone
