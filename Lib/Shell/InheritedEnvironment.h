#pragma once

#include <string_view>

namespace term
{
// A variable a pane's shell must not inherit from whatever started CowTerm: a
// Claude Code session's markers (CLAUDE*, CLAUDECODE), which made every
// Claude started in a pane run without a saved transcript, and COWTERM_PANE,
// which names the pane CowTerm itself was started from. The pane's own
// COWTERM_PANE is set afterwards by whoever opens it.
bool isVariableAPaneMustNotInherit(std::string_view name);
} // namespace term
