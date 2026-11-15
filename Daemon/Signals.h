#pragma once

namespace term
{
// The GUI dying must never take the daemon with it: it ignores the hangup a
// closing parent terminal sends, and the broken pipe a vanished client leaves.
void ignoreHangups();

// Points stdin, stdout and stderr at /dev/null. A daemon started by `ct bridge`
// inherits ssh's pipes, and ssh keeps the connection open while anyone holds them.
void releaseInheritedStreams();
} // namespace term
