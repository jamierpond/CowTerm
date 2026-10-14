#pragma once

namespace term
{
// The GUI dying must never take the daemon with it: it ignores the hangup a
// closing parent terminal sends, and the broken pipe a vanished client leaves.
void ignoreHangups();
} // namespace term
