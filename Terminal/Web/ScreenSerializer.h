#pragma once

#include "../TermScreen.h"

#include <string>

namespace term::web
{
// Repaints a parsed screen as one ANSI byte string: feeding it to a fresh
// terminal of the same cols×rows reproduces the grid, cursor, and the
// input-relevant modes, plus recent scrollback when the primary screen is
// active. The web client plays this into xterm.js on attach, then rides the
// live PTY stream.
std::string serializeScreen(const TermScreen& screen, int scrollbackLines = 200);
} // namespace term::web
