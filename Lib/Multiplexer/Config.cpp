#include "Config.h"
#include "Protocol.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <Miro/Json.h>

namespace term
{
namespace
{
eacp::FilePath configPath()
{
    // A named instance reads its own config file so its settings (and web
    // port) never collide with the user's live instance.
    const auto suffix = proto::instanceSuffix();
    const auto file = suffix.empty() ? "cowterm.json" : "cowterm." + suffix + ".json";
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

std::string writeConfigKeepingUnknownSettings(const AppConfig& config, const std::string& existingText)
{
    constexpr auto indentSpaces = 4;
    auto merged = Miro::Json::parse(Miro::toJSONString(config, indentSpaces));

    try
    {
        const auto existing = Miro::Json::parse(existingText);

        if (existing.isObject())
        {
            for (const auto& [key, value]: existing.asObject())
            {
                merged.asObject().emplace(key, value);
            }
        }
    }
    catch (const std::exception&)
    {
        // An unreadable file has nothing to keep; the known fields are written.
    }

    return Miro::Json::print(merged, indentSpaces);
}

void saveConfig(const AppConfig& config)
{
    const auto text = writeConfigKeepingUnknownSettings(config, eacp::Files::readFile(configPath()));

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
