#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace term
{
using HyperlinkId = std::uint16_t;
inline constexpr HyperlinkId noHyperlink = 0;

// True for the schemes a click may hand to the system opener: http, https,
// mailto and file. Anything else (a custom app scheme) is never opened.
bool hasOpenableScheme(std::string_view uri);

// The URIs of OSC 8 hyperlinks, each stored once; cells hold the small id.
// Ids start at 1, so a cell's zero means "no link".
class HyperlinkTable
{
public:
    // The id for this URI, adding it if it is new; noHyperlink when full.
    HyperlinkId intern(std::string_view uri);

    // Empty for noHyperlink or an id the table doesn't hold.
    std::string_view findUri(HyperlinkId id) const;

    bool isFull() const;
    void clear();

    // Drops every link whose id isUsed doesn't mark (indexed by id) and
    // renumbers the rest; returns each old id's new id.
    std::vector<HyperlinkId> keepOnly(const std::vector<bool>& isUsed);

private:
    std::vector<std::string> uris;
    std::unordered_map<std::string, HyperlinkId> idsByUri;
};
} // namespace term
