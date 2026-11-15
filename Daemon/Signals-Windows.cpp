#include "Signals.h"

namespace term
{
// Windows has no hangup signal; a detached process simply keeps running.
void ignoreHangups() {}

// A detached Windows process gets no console streams to hold.
void releaseInheritedStreams() {}
} // namespace term
