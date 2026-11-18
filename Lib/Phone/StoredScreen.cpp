#include "StoredScreen.h"

#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

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

    // Files from before hosts were named without their user hold "jamiepond@macprorack".
    return StoredScreen {remote::getHostLabel(file->host),
                         file->paneId,
                         file->sessionName,
                         file->savedAtMillisecondsSinceEpoch,
                         std::move(*state)};
}

} // namespace term::phone
