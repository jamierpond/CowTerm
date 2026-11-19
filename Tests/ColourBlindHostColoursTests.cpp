#include "Multiplexer/ColourSpace.h"
#include "Multiplexer/HostColours.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

// The host colours the Mac and the phone share, seen as Jamie (protan or
// deutan), a tritan eye and greyscale see them.

using namespace nano;
using namespace term;

namespace
{
using Matrix = std::array<std::array<float, 3>, 3>;

// Machado, Oliveira and Fernandes 2009, severity 1.0, applied to linear sRGB.
constexpr auto protanopiaSimulation = Matrix {{{0.152286f, 1.052583f, -0.204868f},
                                               {0.114503f, 0.786281f, 0.099216f},
                                               {-0.003882f, -0.048116f, 1.051998f}}};
constexpr auto deuteranopiaSimulation = Matrix {{{0.367322f, 0.860646f, -0.227968f},
                                                 {0.280085f, 0.672501f, 0.047413f},
                                                 {-0.011820f, 0.042940f, 0.968881f}}};
constexpr auto tritanopiaSimulation = Matrix {{{1.255528f, -0.076749f, -0.178779f},
                                               {-0.078411f, 0.930809f, 0.147602f},
                                               {0.004733f, 0.691367f, 0.303900f}}};
constexpr auto normalVision = Matrix {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};

// Linear sRGB to CIE XYZ, D65 white.
constexpr auto linearRgbToXyz = Matrix {{{0.4124f, 0.3576f, 0.1805f},
                                         {0.2126f, 0.7152f, 0.0722f},
                                         {0.0193f, 0.1192f, 0.9505f}}};
constexpr auto whiteX = 0.95047f;
constexpr auto whiteY = 1.0f;
constexpr auto whiteZ = 1.08883f;
constexpr auto labLightnessScale = 116.0f;
constexpr auto labLightnessOffset = 16.0f;
constexpr auto labAScale = 500.0f;
constexpr auto labBScale = 200.0f;
constexpr auto labLinearSegmentEnd = 216.0f / 24389.0f;
constexpr auto labLinearSegmentSlope = 841.0f / 108.0f;
constexpr auto labLinearSegmentOffset = 4.0f / 29.0f;

// CIE76 ΔE of 10 or more reads as a different colour at a glance; 2.3 is the
// smallest difference anyone notices side by side.
constexpr auto minimumColourDifference = 10.0f;

// Two noticeable steps of greyscale lightness (CIE L*) between any two hosts.
constexpr auto minimumGreyscaleLightnessDifference = 4.5f;

// WCAG 2 AA for body text.
constexpr auto minimumTextContrast = 4.5f;

// Jamie's own three hosts get entries further apart than any two hosts must.
constexpr auto minimumColourDifferenceBetweenJamiesHosts = 40.0f;
constexpr auto minimumGreyscaleDifferenceBetweenJamiesHosts = 9.5f;
const auto jamiesHosts = std::vector<std::string> {"jamiepond@big-cow", "jamiepond@tamby", "jamiepond@macprorack"};

struct Lab
{
    float lightness = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
};

LinearRgb multiply(const Matrix& matrix, LinearRgb colour)
{
    const auto row = [&colour](const std::array<float, 3>& weights)
    { return weights[0] * colour.red + weights[1] * colour.green + weights[2] * colour.blue; };

    return {row(matrix[0]), row(matrix[1]), row(matrix[2])};
}

float compressForLab(float ratio)
{
    if (ratio <= labLinearSegmentEnd)
        return labLinearSegmentSlope * ratio + labLinearSegmentOffset;

    return std::cbrt(ratio);
}

Lab convertToLab(LinearRgb colour)
{
    const auto xyz = multiply(linearRgbToXyz, colour);
    const auto x = compressForLab(xyz.red / whiteX);
    const auto y = compressForLab(xyz.green / whiteY);
    const auto z = compressForLab(xyz.blue / whiteZ);
    return {labLightnessScale * y - labLightnessOffset, labAScale * (x - y), labBScale * (y - z)};
}

LinearRgb clampToDisplay(LinearRgb colour)
{
    return {std::clamp(colour.red, 0.0f, 1.0f),
            std::clamp(colour.green, 0.0f, 1.0f),
            std::clamp(colour.blue, 0.0f, 1.0f)};
}

Lab simulateSeeing(const Matrix& vision, Rgb colour)
{
    return convertToLab(clampToDisplay(multiply(vision, convertToLinearRgb(colour))));
}

float calculateColourDifference(const Matrix& vision, Rgb first, Rgb second)
{
    const auto one = simulateSeeing(vision, first);
    const auto other = simulateSeeing(vision, second);
    return std::hypot(one.lightness - other.lightness, one.a - other.a, one.b - other.b);
}

float calculateGreyscaleDifference(Rgb first, Rgb second)
{
    const auto one = convertLuminanceToLightness(calculateRelativeLuminance(first));
    const auto other = convertLuminanceToLightness(calculateRelativeLuminance(second));
    return std::abs(one - other);
}

using ColourDifference = std::function<float(Rgb, Rgb)>;

float findSmallestDifference(const std::vector<Rgb>& colours, const ColourDifference& difference)
{
    auto smallest = std::numeric_limits<float>::max();

    for (auto first = std::size_t {0}; first < colours.size(); ++first)
    {
        for (auto second = first + 1; second < colours.size(); ++second)
            smallest = std::min(smallest, difference(colours[first], colours[second]));
    }

    return smallest;
}

ColourDifference seenWith(const Matrix& vision)
{
    return [&vision](Rgb first, Rgb second) { return calculateColourDifference(vision, first, second); };
}

std::vector<Rgb> listPaletteBackgrounds()
{
    auto backgrounds = std::vector<Rgb> {};

    for (const auto& entry: listColourBlindSafeHostRowColours())
        backgrounds.push_back(entry.background);

    return backgrounds;
}

std::vector<Rgb> listJamiesHostBackgrounds()
{
    auto backgrounds = std::vector<Rgb> {};

    for (const auto& host: jamiesHosts)
        backgrounds.push_back(getColourBlindSafeHostRowColours(AppConfig {}, host).background);

    return backgrounds;
}
} // namespace

auto paletteStaysApartForColourBlindEyes = test("ColourBlindHostColours/everyPairStaysApartForColourBlindEyes") = []
{
    const auto backgrounds = listPaletteBackgrounds();

    check(findSmallestDifference(backgrounds, seenWith(normalVision)) >= minimumColourDifference);
    check(findSmallestDifference(backgrounds, seenWith(protanopiaSimulation)) >= minimumColourDifference);
    check(findSmallestDifference(backgrounds, seenWith(deuteranopiaSimulation)) >= minimumColourDifference);
};

auto paletteStaysApartInGreyscale = test("ColourBlindHostColours/everyPairStaysApartInGreyscale") = []
{
    check(findSmallestDifference(listPaletteBackgrounds(), calculateGreyscaleDifference)
          >= minimumGreyscaleLightnessDifference);
};

auto rowTextReadsOnEveryBackground = test("ColourBlindHostColours/rowTextReadsOnEveryBackground") = []
{
    for (const auto& entry: listColourBlindSafeHostRowColours())
        check(calculateContrastRatio(entry.text, entry.background) >= minimumTextContrast);
};

auto jamiesHostsAreUnmistakable = test("ColourBlindHostColours/jamiesThreeHostsAreUnmistakable") = []
{
    const auto backgrounds = listJamiesHostBackgrounds();

    check(findSmallestDifference(backgrounds, seenWith(protanopiaSimulation))
          >= minimumColourDifferenceBetweenJamiesHosts);
    check(findSmallestDifference(backgrounds, seenWith(deuteranopiaSimulation))
          >= minimumColourDifferenceBetweenJamiesHosts);
    check(findSmallestDifference(backgrounds, calculateGreyscaleDifference)
          >= minimumGreyscaleDifferenceBetweenJamiesHosts);
};

auto configuredColourWinsOnThePhone = test("ColourBlindHostColours/configuredColourWinsAndStaysReadable") = []
{
    constexpr auto paleConfiguredColour = Rgb {0xf6c177};
    auto config = AppConfig {};
    config.hosts["tamby"].color = "#f6c177";

    const auto colours = getColourBlindSafeHostRowColours(config, "jamiepond@tamby");
    check(colours.stripe == paleConfiguredColour);
    check(colours.background != paleConfiguredColour, "too light for white text, so darkened");
    check(calculateContrastRatio(colours.text, colours.background) >= minimumTextContrast);
};

// Not every pair of the palette passes for tritanopia (two entries come within
// ΔE 8.2), but Jamie's three hosts do.
auto jamiesHostsStayApartForTritanopia = test("ColourBlindHostColours/jamiesThreeHostsStayApartForTritanopia") = []
{
    check(findSmallestDifference(listJamiesHostBackgrounds(), seenWith(tritanopiaSimulation))
          >= minimumColourDifference);
};

// No fourth palette entry stays ΔE 40 from the other three, so tamby-windows
// (reddish purple) is held to the palette-wide rule against Jamie's three.
auto tambyWindowsStaysApartFromJamiesOtherHosts =
    test("ColourBlindHostColours/tambyWindowsStaysApartFromJamiesOtherHosts") = []
{
    const auto tambyWindows = getColourBlindSafeHostRowColours(AppConfig {}, "jamie@tamby-windows").background;

    for (const auto& otherHost: listJamiesHostBackgrounds())
    {
        check(calculateColourDifference(protanopiaSimulation, tambyWindows, otherHost) >= minimumColourDifference);
        check(calculateColourDifference(deuteranopiaSimulation, tambyWindows, otherHost) >= minimumColourDifference);
        check(calculateGreyscaleDifference(tambyWindows, otherHost) >= minimumGreyscaleLightnessDifference);
    }
};
