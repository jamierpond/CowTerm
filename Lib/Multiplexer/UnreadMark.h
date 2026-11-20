#pragma once

#include "Emulator/TermTypes.h"

#include <CowTermPalette/UnreadMark.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The mark the Mac and the phone draw on a session or pane Jamie has not seen
// (PaneReadState): a pill filled with Okabe-Ito yellow that names the event in
// words, so it reads without telling colours apart.
namespace term
{
// The newer of two marks; either may be empty.
std::optional<UnreadMark> chooseNewerUnreadMark(const std::optional<UnreadMark>& first,
                                                const std::optional<UnreadMark>& second);

// The pill's word: "finished", "waiting", "bell", "notified".
std::string describeUnreadMark(const UnreadMark& mark);

// Black: the pill's fill is the brightest colour in the palette.
Rgb getUnreadMarkTextColour();

} // namespace term
