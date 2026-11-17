#include "ColourSpace.h"

#include <algorithm>
#include <cmath>

namespace term
{
namespace
{
constexpr auto channelMask = Rgb {0xff};
constexpr auto channelMaximum = 255.0f;
constexpr auto redShift = 16;
constexpr auto greenShift = 8;

// The sRGB transfer curve (IEC 61966-2-1).
constexpr auto srgbLinearSegmentEnd = 0.04045f;
constexpr auto srgbLinearSegmentEndInLinearLight = 0.0031308f;
constexpr auto srgbLinearSegmentSlope = 12.92f;
constexpr auto srgbCurveOffset = 0.055f;
constexpr auto srgbCurveExponent = 2.4f;

// WCAG 2's luminance weights and contrast flare.
constexpr auto redLuminanceWeight = 0.2126f;
constexpr auto greenLuminanceWeight = 0.7152f;
constexpr auto blueLuminanceWeight = 0.0722f;
constexpr auto contrastFlare = 0.05f;

// CIE 1976 L*.
constexpr auto lightnessScale = 116.0f;
constexpr auto lightnessOffset = 16.0f;
constexpr auto lightnessLinearSegmentSlope = 903.3f;
constexpr auto lightnessLinearSegmentEnd = 216.0f / 24389.0f;
constexpr auto lightnessLinearSegmentEndAsLightness = 8.0f;

float decodeChannel(Rgb colour, int shift)
{
    const auto encoded = static_cast<float>((colour >> shift) & channelMask) / channelMaximum;

    if (encoded <= srgbLinearSegmentEnd)
        return encoded / srgbLinearSegmentSlope;

    return std::pow((encoded + srgbCurveOffset) / (1.0f + srgbCurveOffset), srgbCurveExponent);
}

Rgb encodeChannel(float linear, int shift)
{
    const auto clamped = std::clamp(linear, 0.0f, 1.0f);
    auto encoded = clamped * srgbLinearSegmentSlope;

    if (clamped > srgbLinearSegmentEndInLinearLight)
        encoded = (1.0f + srgbCurveOffset) * std::pow(clamped, 1.0f / srgbCurveExponent) - srgbCurveOffset;

    const auto level = static_cast<Rgb>(std::lround(encoded * channelMaximum));
    return (level & channelMask) << shift;
}
} // namespace

LinearRgb convertToLinearRgb(Rgb colour)
{
    return {decodeChannel(colour, redShift), decodeChannel(colour, greenShift), decodeChannel(colour, 0)};
}

Rgb convertToRgb(LinearRgb colour)
{
    return encodeChannel(colour.red, redShift) | encodeChannel(colour.green, greenShift)
           | encodeChannel(colour.blue, 0);
}

float calculateRelativeLuminance(LinearRgb colour)
{
    return colour.red * redLuminanceWeight + colour.green * greenLuminanceWeight
           + colour.blue * blueLuminanceWeight;
}

float calculateRelativeLuminance(Rgb colour)
{
    return calculateRelativeLuminance(convertToLinearRgb(colour));
}

float calculateContrastRatio(Rgb first, Rgb second)
{
    const auto firstLuminance = calculateRelativeLuminance(first) + contrastFlare;
    const auto secondLuminance = calculateRelativeLuminance(second) + contrastFlare;
    return std::max(firstLuminance, secondLuminance) / std::min(firstLuminance, secondLuminance);
}

float convertLuminanceToLightness(float relativeLuminance)
{
    if (relativeLuminance <= lightnessLinearSegmentEnd)
        return relativeLuminance * lightnessLinearSegmentSlope;

    return lightnessScale * std::cbrt(relativeLuminance) - lightnessOffset;
}

float convertLightnessToLuminance(float lightness)
{
    if (lightness <= lightnessLinearSegmentEndAsLightness)
        return lightness / lightnessLinearSegmentSlope;

    const auto cubeRoot = (lightness + lightnessOffset) / lightnessScale;
    return cubeRoot * cubeRoot * cubeRoot;
}
} // namespace term
