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

// How a host's session rows are drawn, on the Mac and the phone alike: the
// background is the host's colour darkened so the light text reads on it, the
// stripe is that colour at full strength.
struct HostRowColours
{
    Rgb stripe = 0;
    Rgb background = 0;
    Rgb text = 0;
};

// A host's colours (docs/iOSDesign.md): hosts.<name>.color from the config
// when set, otherwise an entry of the Okabe-Ito palette picked from the host
// name without its user, so the Mac and the phone agree. The entries stay apart
// for protanopia, deuteranopia and in greyscale.
HostRowColours getColourBlindSafeHostRowColours(const AppConfig& config, const std::string& host);

// Every entry of that palette, in the order host names pick them.
std::vector<HostRowColours> listColourBlindSafeHostRowColours();

// The unread mark's fill (UnreadMark.h): Okabe-Ito yellow at full strength,
// lighter than every host's row fill, so it stands out on all of them.
Rgb getUnreadMarkFillColour();

// "#eb6f92" -> 0xeb6f92; empty for anything else.
std::optional<Rgb> parseHexColour(const std::string& text);

// Mixes amount (0..1) of over into base, per channel.
Rgb blendColours(Rgb base, Rgb over, float amount);
} // namespace term
