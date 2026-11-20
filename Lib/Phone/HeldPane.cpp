#include "HeldPane.h"

#include "JsonFile.h"

#include "Multiplexer/RemoteLocation.h"

#include <Miro/Reflect.h>

namespace term::phone
{
namespace
{
struct HeldPaneFile
{
    std::string host;
    std::string paneId;
    int columns = 0;
    int rows = 0;

    MIRO_REFLECT(host, paneId, columns, rows)
};
} // namespace

bool saveHeldPane(const std::filesystem::path& path, const HeldPane& pane)
{
    const auto file =
        HeldPaneFile {pane.host, pane.paneId, pane.sizeBeforeThePhone.cols, pane.sizeBeforeThePhone.rows};

    return writeJsonFileAtomically(path, file);
}

std::optional<HeldPane> loadHeldPane(const std::filesystem::path& path)
{
    const auto file = readJsonFile<HeldPaneFile>(path);

    if (!file)
        return std::nullopt;

    // Files from before hosts were named without their user hold "jamiepond@macprorack".
    return HeldPane {remote::getHostLabel(file->host), file->paneId, {file->columns, file->rows}};
}

void forgetHeldPane(const std::filesystem::path& path)
{
    auto ignoredError = std::error_code {};
    std::filesystem::remove(path, ignoredError);
}
} // namespace term::phone
