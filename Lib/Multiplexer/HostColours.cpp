#include "HostColours.h"

#include "ColourSpace.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace term
{
namespace
{
// Rosé Pine's accents: distinct from each other and readable on its base.
// Hosts get them muted (see mutedPaletteGreyAmount).
constexpr auto hostPalette = std::array<Rgb, 6> {0xeb6f92, 0xf6c177, 0xebbcba, 0x31748f, 0x9ccfd8, 0xc4a7e7};

// How far each accent moves toward a grey of its own brightness: enough to
// sit quietly behind text, not so far that hosts look alike.
constexpr auto mutedPaletteGreyAmount = 0.45f;
constexpr auto redLuminanceWeight = 0.299f;
constexpr auto greenLuminanceWeight = 0.587f;
constexpr auto blueLuminanceWeight = 0.114f;

// Okabe and Ito's colours for colour-blind readers (jfly.uni-koeln.de/color).
constexpr auto okabeItoOrange = Rgb {0xe69f00};
constexpr auto okabeItoSkyBlue = Rgb {0x56b4e9};
constexpr auto okabeItoBluishGreen = Rgb {0x009e73};
constexpr auto okabeItoYellow = Rgb {0xf0e442};
constexpr auto okabeItoBlue = Rgb {0x0072b2};
constexpr auto okabeItoVermillion = Rgb {0xd55e00};
constexpr auto okabeItoReddishPurple = Rgb {0xcc79a7};

// Row backgrounds are spread over CIE L* 18 to 48 in steps of 5, so hosts also
// differ in greyscale; 48 keeps white text at a contrast of 4.5:1 or more.
constexpr auto darkestRowLightness = 18.0f;
constexpr auto rowLightnessStep = 5.0f;
constexpr auto lightestRowLightness = 48.0f;
constexpr auto hostRowTextColour = Rgb {0xffffff};

struct ColourBlindSafeEntry
{
    Rgb colour;
    int lightnessSteps;
};

// Neighbours differ by at least two lightness steps. FNV-1a picks entry 0 for
// big-cow, 1 for macprorack and 3 for tamby: orange, bluish green and blue,
// the three that stay furthest apart under protanopia and deuteranopia.
constexpr auto colourBlindSafePalette = std::array {
    ColourBlindSafeEntry {okabeItoOrange, 6},
    ColourBlindSafeEntry {okabeItoBluishGreen, 2},
    ColourBlindSafeEntry {okabeItoYellow, 0},
    ColourBlindSafeEntry {okabeItoBlue, 4},
    ColourBlindSafeEntry {okabeItoSkyBlue, 1},
    ColourBlindSafeEntry {okabeItoReddishPurple, 5},
    ColourBlindSafeEntry {okabeItoVermillion, 3},
};

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

Rgb calculateGreyOfSameBrightness(Rgb colour)
{
    const auto red = static_cast<float>((colour >> redShift) & channelMask);
    const auto green = static_cast<float>((colour >> greenShift) & channelMask);
    const auto blue = static_cast<float>(colour & channelMask);
    const auto luminance = red * redLuminanceWeight + green * greenLuminanceWeight + blue * blueLuminanceWeight;
    const auto level = static_cast<Rgb>(luminance) & channelMask;
    return (level << redShift) | (level << greenShift) | level;
}

// Scales the colour in linear light, keeping its hue and saturation.
Rgb darkenToLightness(Rgb colour, float lightness)
{
    const auto linear = convertToLinearRgb(colour);
    const auto luminance = calculateRelativeLuminance(linear);

    if (luminance <= 0.0f)
        return colour;

    const auto scale = convertLightnessToLuminance(lightness) / luminance;
    return convertToRgb({linear.red * scale, linear.green * scale, linear.blue * scale});
}

HostRowColours makeHostRowColours(const ColourBlindSafeEntry& entry)
{
    const auto lightness = darkestRowLightness + rowLightnessStep * static_cast<float>(entry.lightnessSteps);
    return {entry.colour, darkenToLightness(entry.colour, lightness), hostRowTextColour};
}

// A configured colour too light for white text is darkened just enough.
HostRowColours makeHostRowColours(Rgb configuredColour)
{
    const auto ownLightness = convertLuminanceToLightness(calculateRelativeLuminance(configuredColour));
    const auto lightness = std::min(ownLightness, lightestRowLightness);
    return {configuredColour, darkenToLightness(configuredColour, lightness), hostRowTextColour};
}

std::optional<Rgb> findConfiguredHostColour(const AppConfig& config, const std::string& host)
{
    for (const auto& name: {host, getHostLabel(host)})
    {
        if (auto found = config.hosts.find(name); found != config.hosts.end())
        {
            if (const auto colour = parseHexColour(found->second.color))
                return colour;
        }
    }

    return std::nullopt;
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

Rgb getHostColour(const AppConfig& config, const std::string& host)
{
    if (const auto configured = findConfiguredHostColour(config, host))
        return *configured;

    return muteColour(hostPalette[hashHostName(getHostLabel(host)) % hostPalette.size()]);
}

HostRowColours getColourBlindSafeHostRowColours(const AppConfig& config, const std::string& host)
{
    if (const auto configured = findConfiguredHostColour(config, host))
        return makeHostRowColours(*configured);

    const auto index = hashHostName(getHostLabel(host)) % colourBlindSafePalette.size();
    return makeHostRowColours(colourBlindSafePalette[index]);
}

std::vector<HostRowColours> listColourBlindSafeHostRowColours()
{
    auto entries = std::vector<HostRowColours> {};

    for (const auto& entry: colourBlindSafePalette)
        entries.push_back(makeHostRowColours(entry));

    return entries;
}

Rgb blendColours(Rgb base, Rgb over, float amount)
{
    return blendChannel(base, over, redShift, amount) | blendChannel(base, over, greenShift, amount)
           | blendChannel(base, over, 0, amount);
}
} // namespace term

namespace term
{
Rgb muteColour(Rgb colour)
{
    return blendColours(colour, calculateGreyOfSameBrightness(colour), mutedPaletteGreyAmount);
}
} // namespace term
