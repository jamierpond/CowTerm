#include "Signals.h"

namespace term
{
// Windows has no hangup signal; a detached process simply keeps running.
void ignoreHangups() {}
} // namespace term
