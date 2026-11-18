#include "SessionOpenings.h"

#include "Multiplexer/RemoteLocation.h"

#include <FrecencySearch/LearnedPicks.h>
#include <Miro/Reflect.h>

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <algorithm>
#include <vector>

namespace term::phone
{
namespace
{
// The weight of a pick in the Mac's PaletteUsageStore.
constexpr auto openingWeight = 1.0;

struct SessionOpeningEntry
{
    std::string rowKey;
    double count = 0.0;
    std::int64_t at = 0;

    MIRO_REFLECT(rowKey, count, at)
};

struct SessionOpeningsFile
{
    std::vector<SessionOpeningEntry> sessions;

    MIRO_REFLECT(sessions)
};

// Openings from before hosts were named without their user were recorded
// under "jamiepond@macprorack/iac"; they count under "macprorack/iac" now. Read
// oldest first, a session recorded under both names keeps the one opened last.
void moveOpeningsToHostNamesWithoutUser(SessionOpeningsFile& file)
{
    for (auto& entry: file.sessions)
    {
        const auto hostEnd = std::min(entry.rowKey.find('/'), entry.rowKey.size());
        entry.rowKey = remote::getHostLabel(entry.rowKey.substr(0, hostEnd)) + entry.rowKey.substr(hostEnd);
    }

    std::ranges::stable_sort(file.sessions, {}, &SessionOpeningEntry::at);
}
} // namespace

SessionOpenings::SessionOpenings(std::filesystem::path pathToUse)
    : path(std::move(pathToUse))
{
    const auto text = eacp::Files::readFile(eacp::FilePath {path.string()});

    if (text.empty())
        return;

    try
    {
        auto file = SessionOpeningsFile {};
        Miro::fromJSONString(file, text);
        moveOpeningsToHostNamesWithoutUser(file);

        for (const auto& entry: file.sessions)
            usageByRowKey[entry.rowKey] = {entry.count, entry.at};
    }
    catch (const std::exception&)
    {
        usageByRowKey.clear();
    }
}

void SessionOpenings::recordOpening(const std::string& rowKey, std::int64_t nowMilliseconds)
{
    usageByRowKey[rowKey] = {getUsage(rowKey, nowMilliseconds) + openingWeight, nowMilliseconds};
    save();
}

double SessionOpenings::getUsage(const std::string& rowKey, std::int64_t nowMilliseconds) const
{
    const auto found = usageByRowKey.find(rowKey);

    if (found == usageByRowKey.end())
        return 0.0;

    return frecencysearch::calculateDecayedUsage(found->second.count, found->second.atMilliseconds, nowMilliseconds);
}

std::optional<std::int64_t> SessionOpenings::getLastOpeningMilliseconds(const std::string& rowKey) const
{
    const auto found = usageByRowKey.find(rowKey);

    if (found == usageByRowKey.end())
        return std::nullopt;

    return found->second.atMilliseconds;
}

void SessionOpenings::save() const
{
    auto file = SessionOpeningsFile {};

    for (const auto& [rowKey, usage]: usageByRowKey)
        file.sessions.push_back({rowKey, usage.count, usage.atMilliseconds});

    auto ignoredError = std::error_code {};
    std::filesystem::create_directories(path.parent_path(), ignoredError);

    try
    {
        eacp::Files::writeFileAtomically(eacp::FilePath {path.string()}, Miro::toJSONString(file));
    }
    // As on the Mac, losing the last openings is harmless.
    catch (const std::exception&)
    {
    }
}
} // namespace term::phone
