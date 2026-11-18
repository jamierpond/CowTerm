#include "HeldPane.h"

#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

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

std::optional<HeldPane> loadHeldPane(const std::filesystem::path& path)
{
    const auto text = eacp::Files::readFile(eacp::FilePath {path.string()});

    if (text.empty())
        return std::nullopt;

    try
    {
        auto file = HeldPaneFile {};
        Miro::fromJSONString(file, text);
        // Files from before hosts were named without their user hold "jamiepond@macprorack".
        return HeldPane {remote::getHostLabel(file.host), file.paneId, {file.columns, file.rows}};
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

void forgetHeldPane(const std::filesystem::path& path)
{
    auto ignoredError = std::error_code {};
    std::filesystem::remove(path, ignoredError);
}
} // namespace term::phone
