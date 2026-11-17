#pragma once

#include "TerminalView/TerminalView.h"

namespace cowterm::mobile
{
// The pane screen's shell while no pane is chosen yet (the overview is on
// screen): it shows nothing and sends nothing anywhere.
term::TerminalConfig makeNoPaneYetConfig();
} // namespace cowterm::mobile
