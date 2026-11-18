#include "DataDirectories.h"
#include "Protocol.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/StdPath.h>

namespace term
{
namespace
{
std::string addInstanceSuffix(const std::string& name)
{
    const auto suffix = proto::instanceSuffix();
    return suffix.empty() ? name : name + "-" + suffix;
}

// The emberstore folders CowTerm used before pond/CowTerm.
std::filesystem::path getOldTamberDirectory(const std::string& name)
{
    return eacp::toStdPath(eacp::FilePath::appDataDirectory()) / "tamber"
           / addInstanceSuffix(name);
}

std::filesystem::path getOldLogsDirectory()
{
    return eacp::toStdPath(eacp::FilePath::appDataDirectory()) / "CowTerm" / "logs";
}
} // namespace

std::filesystem::path getDataDirectory()
{
    return pondapp::getPondAppDataDirectory("CowTerm");
}

std::filesystem::path getLogsDirectory()
{
    return getDataDirectory() / "logs";
}

std::filesystem::path getSessionStoreDirectory()
{
    return getDataDirectory() / addInstanceSuffix("sessions");
}

std::filesystem::path getPaneStoreDirectory()
{
    return getDataDirectory() / addInstanceSuffix("panes");
}

std::filesystem::path getLineCoverageDirectory()
{
    return getDataDirectory() / addInstanceSuffix("coverage");
}

std::vector<pondapp::DataDirectoryMove> moveOldAppData()
{
    auto moves = std::vector<pondapp::DataDirectoryMove> {};

    if (auto move = pondapp::moveOldDataDirectory(getOldLogsDirectory(), getLogsDirectory()))
        moves.push_back(*move);

    if (auto move = pondapp::moveOldDataDirectory(getOldTamberDirectory("cowterm"),
                                                  getSessionStoreDirectory()))
        moves.push_back(*move);

    return moves;
}

std::optional<pondapp::DataDirectoryMove> moveOldPaneStore()
{
    return pondapp::moveOldDataDirectory(getOldTamberDirectory("cowterm-panes"),
                                         getPaneStoreDirectory());
}
} // namespace term
