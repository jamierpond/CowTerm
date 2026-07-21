#include "Config.h"
#include "Protocol.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

namespace term
{
namespace
{
// A named dev instance reads and writes its own file, so running a build
// under test cannot inherit — or overwrite, via the zoom keys — the
// settings of the CowTerm you actually use. See proto::instanceSuffix.
eacp::FilePath configPath()
{
    const auto suffix = proto::instanceSuffix();
    const auto file =
        suffix.empty() ? "cowterm.json" : "cowterm." + suffix + ".json";

    return eacp::FilePath::homeDirectory() / ".config" / file;
}
} // namespace

std::string expandHome(const std::string& path)
{
    if (path.empty() || path[0] != '~')
        return path;

    return eacp::FilePath::homeDirectory().str() + path.substr(1);
}

AppConfig loadConfig()
{
    auto config = AppConfig {};
    const auto text = eacp::Files::readFile(configPath());

    if (!text.empty())
        Miro::fromJSONString(config, text);

    return config;
}

void saveConfig(const AppConfig& config)
{
    const auto text = Miro::toJSONString(config, 4);

    try
    {
        eacp::Files::writeFileAtomically(
            configPath(),
            {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }
    catch (const std::exception&)
    {
    }
}

Theme themeByName(const std::string& name)
{
    if (name == "rosepine")
    {
        auto theme = Theme {};
        theme.background = 0x191724;
        theme.foreground = 0xe0def4;
        theme.cursor = 0xe0def4;
        theme.selection = 0x403d52;
        theme.paneBorder = 0x26233a;
        theme.paneBorderActive = 0x56526e;
        theme.ansi = {0x26233a,
                      0xeb6f92,
                      0x31748f,
                      0xf6c177,
                      0x9ccfd8,
                      0xc4a7e7,
                      0xebbcba,
                      0xe0def4,
                      0x6e6a86,
                      0xeb6f92,
                      0x31748f,
                      0xf6c177,
                      0x9ccfd8,
                      0xc4a7e7,
                      0xebbcba,
                      0xe0def4};
        return theme;
    }

    return Theme {};
}
} // namespace term
