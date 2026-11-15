#include "StandardStreams.h"

#include <csignal>
#include <unistd.h>

namespace term::bridge
{
void prepareStandardStreams()
{
    std::signal(SIGPIPE, SIG_IGN);
}

long readStandardInput(char* buffer, std::size_t size)
{
    return ::read(STDIN_FILENO, buffer, size);
}

long writeStandardOutput(const char* bytes, std::size_t size)
{
    return ::write(STDOUT_FILENO, bytes, size);
}
} // namespace term::bridge
