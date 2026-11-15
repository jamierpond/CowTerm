#include "Signals.h"

#include <csignal>
#include <fcntl.h>
#include <initializer_list>
#include <unistd.h>

namespace term
{
void ignoreHangups()
{
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

void releaseInheritedStreams()
{
    const auto nullDevice = open("/dev/null", O_RDWR);

    if (nullDevice < 0)
        return;

    for (const auto stream: {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})
        dup2(nullDevice, stream);

    if (nullDevice > STDERR_FILENO)
        close(nullDevice);
}
} // namespace term
