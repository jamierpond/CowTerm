#pragma once

#include "Emulator/TermTypes.h"

namespace term
{
// sRGB channels with the gamma curve taken off, 0 to 1: the space light adds up in.
struct LinearRgb
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

LinearRgb convertToLinearRgb(Rgb colour);

// Channels outside 0 to 1 are clamped.
Rgb convertToRgb(LinearRgb colour);

// WCAG 2's relative luminance: 0 for black, 1 for white.
float calculateRelativeLuminance(LinearRgb colour);
float calculateRelativeLuminance(Rgb colour);

// WCAG 2's contrast ratio, from 1 (the same) to 21 (black on white).
float calculateContrastRatio(Rgb first, Rgb second);

// CIE L*: lightness as the eye judges it, 0 for black, 100 for white.
float convertLuminanceToLightness(float relativeLuminance);
float convertLightnessToLuminance(float lightness);
} // namespace term
