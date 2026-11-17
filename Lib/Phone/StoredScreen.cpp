#include "StoredScreen.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <array>

namespace term::phone
{
namespace
{
// The file's layout. The terminal state is the exact JSON the daemon sends in
// a `state` message, so loading it checks the format version the same way.
struct StoredScreenFile
{
    std::string host;
    std::string paneId;
    std::string sessionName;
    std::int64_t savedAtMillisecondsSinceEpoch = 0;
    std::string terminalState;

    MIRO_REFLECT(host, paneId, sessionName, savedAtMillisecondsSinceEpoch, terminalState)
};

struct AgeUnit
{
    std::int64_t milliseconds;
    std::string_view suffix;
};

constexpr auto millisecondsPerSecond = std::int64_t {1000};
constexpr auto millisecondsPerMinute = 60 * millisecondsPerSecond;
constexpr auto millisecondsPerHour = 60 * millisecondsPerMinute;
constexpr auto millisecondsPerDay = 24 * millisecondsPerHour;

// Largest first: an age is told in the largest unit it has a whole one of.
constexpr auto ageUnits = std::array {
    AgeUnit {millisecondsPerDay, " d ago"},
    AgeUnit {millisecondsPerHour, " h ago"},
    AgeUnit {millisecondsPerMinute, " min ago"},
    AgeUnit {millisecondsPerSecond, " s ago"},
};

std::optional<StoredScreenFile> readStoredScreenFile(const std::filesystem::path& path)
{
    const auto text = eacp::Files::readFile(eacp::FilePath {path.string()});

    if (text.empty())
        return std::nullopt;

    try
    {
        auto file = StoredScreenFile {};
        Miro::fromJSONString(file, text);
        return file;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}
} // namespace

bool saveStoredScreen(const std::filesystem::path& path, const StoredScreen& screen)
{
    const auto file = StoredScreenFile {screen.host,
                                        screen.paneId,
                                        screen.sessionName,
                                        screen.savedAtMillisecondsSinceEpoch,
                                        serializeTerminalState(screen.state)};

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

std::optional<StoredScreen> loadStoredScreen(const std::filesystem::path& path)
{
    const auto file = readStoredScreenFile(path);

    if (!file)
        return std::nullopt;

    auto state = parseTerminalState(file->terminalState);

    if (!state)
        return std::nullopt;

    return StoredScreen {file->host,
                         file->paneId,
                         file->sessionName,
                         file->savedAtMillisecondsSinceEpoch,
                         std::move(*state)};
}

std::string describeStoredScreenAge(std::int64_t ageMilliseconds)
{
    for (const auto& unit: ageUnits)
    {
        if (ageMilliseconds >= unit.milliseconds)
            return std::to_string(ageMilliseconds / unit.milliseconds) + std::string {unit.suffix};
    }

    return "just now";
}
} // namespace term::phone
