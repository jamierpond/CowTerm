#pragma once

#include "Config.h"
#include "RemoteLocation.h"

#include "Emulator/TermTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace term
{
using remote::getHostLabel;

// A remote host's colour: hosts.<name>.color from the config when set,
// otherwise picked from the host name alone, so every machine agrees.
Rgb getHostColour(const AppConfig& config, const std::string& host);

// How the phone draws a host's session rows: the background is the host's
// colour darkened so the light text reads on it, the stripe is that colour at
// full strength.
struct HostRowColours
{
    Rgb stripe = 0;
    Rgb background = 0;
    Rgb text = 0;
};

// The phone's host colours (docs/iOSDesign.md): hosts.<name>.color from the
// config when set, otherwise an entry of the Okabe-Ito palette picked from the
// host name. The entries stay apart for protanopia, deuteranopia and in
// greyscale, which the Mac's getHostColour does not promise.
HostRowColours getColourBlindSafeHostRowColours(const AppConfig& config, const std::string& host);

// Every entry of that palette, in the order host names pick them.
std::vector<HostRowColours> listColourBlindSafeHostRowColours();

// "#eb6f92" -> 0xeb6f92; empty for anything else.
std::optional<Rgb> parseHexColour(const std::string& text);

// The colour moved part way toward a grey of the same brightness: what
// every derived host colour gets, so tints stay quiet on the theme.
Rgb muteColour(Rgb colour);

// Mixes amount (0..1) of over into base, per channel.
Rgb blendColours(Rgb base, Rgb over, float amount);
} // namespace term
