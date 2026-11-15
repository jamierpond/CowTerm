#include "HostColours.h"

#include <array>
#include <charconv>

namespace term
{
namespace
{
// Rosé Pine's accents: distinct from each other and readable on its base.
constexpr auto hostPalette = std::array<Rgb, 6> {0xeb6f92, 0xf6c177, 0xebbcba, 0x31748f, 0x9ccfd8, 0xc4a7e7};

constexpr auto hexDigitCount = std::size_t {6};
constexpr auto hexBase = 16;
constexpr auto fnvOffsetBasis = std::uint32_t {2166136261u};
constexpr auto fnvPrime = std::uint32_t {16777619u};
constexpr auto channelMask = Rgb {0xff};
constexpr auto redShift = 16;
constexpr auto greenShift = 8;

// FNV-1a: the same on every machine and every build, unlike std::hash.
std::uint32_t hashHostName(const std::string& host)
{
    auto hash = fnvOffsetBasis;

    for (const auto character: host)
    {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= fnvPrime;
    }

    return hash;
}

Rgb blendChannel(Rgb base, Rgb over, int shift, float amount)
{
    const auto from = static_cast<float>((base >> shift) & channelMask);
    const auto to = static_cast<float>((over >> shift) & channelMask);
    const auto mixed = static_cast<Rgb>(from + (to - from) * amount);
    return (mixed & channelMask) << shift;
}
} // namespace

std::optional<Rgb> parseHexColour(const std::string& text)
{
    if (text.size() != hexDigitCount + 1 || text[0] != '#')
        return std::nullopt;

    auto value = Rgb {0};
    const auto* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data() + 1, end, value, hexBase);

    if (error != std::errc {} || stop != end)
        return std::nullopt;

    return value;
}

std::string getHostLabel(const std::string& host)
{
    const auto at = host.find('@');
    return at == std::string::npos ? host : host.substr(at + 1);
}

Rgb getHostColour(const AppConfig& config, const std::string& host)
{
    const auto label = getHostLabel(host);

    for (const auto& name: {host, label})
    {
        if (auto found = config.hosts.find(name); found != config.hosts.end())
        {
            if (const auto colour = parseHexColour(found->second.color))
                return *colour;
        }
    }

    return hostPalette[hashHostName(label) % hostPalette.size()];
}

Rgb blendColours(Rgb base, Rgb over, float amount)
{
    return blendChannel(base, over, redShift, amount) | blendChannel(base, over, greenShift, amount)
           | blendChannel(base, over, 0, amount);
}
} // namespace term
