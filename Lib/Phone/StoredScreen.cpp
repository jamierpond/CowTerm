#include "StoredScreen.h"

#include "JsonFile.h"

#include "Multiplexer/RemoteLocation.h"

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
} // namespace

bool saveStoredScreen(const std::filesystem::path& path, const StoredScreen& screen)
{
    const auto file = StoredScreenFile {screen.host,
                                        screen.paneId,
                                        screen.sessionName,
                                        screen.savedAtMillisecondsSinceEpoch,
                                        serializeTerminalState(screen.state)};

    return writeJsonFileAtomically(path, file);
}

std::optional<StoredScreen> loadStoredScreen(const std::filesystem::path& path)
{
    const auto file = readJsonFile<StoredScreenFile>(path);

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
