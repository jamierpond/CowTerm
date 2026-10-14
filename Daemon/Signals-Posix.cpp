#include "Signals.h"

#include <csignal>

namespace term
{
void ignoreHangups()
{
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}
} // namespace term
