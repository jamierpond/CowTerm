#pragma once

#include "Config.h"

#include "CowTermCore/TermTypes.h"

#include <optional>
#include <string>

namespace term
{
// A remote host's colour: hosts.<name>.color from the config when set,
// otherwise picked from the host name alone, so every machine agrees.
Rgb getHostColour(const AppConfig& config, const std::string& host);

// "#eb6f92" -> 0xeb6f92; empty for anything else.
std::optional<Rgb> parseHexColour(const std::string& text);

// How a host is shown: "tamby" for "jamiepond@tamby".
std::string getHostLabel(const std::string& host);

// Mixes amount (0..1) of over into base, per channel.
Rgb blendColours(Rgb base, Rgb over, float amount);
} // namespace term
