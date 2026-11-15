#include "StandardStreams.h"

#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <limits>

namespace term::bridge
{
namespace
{
constexpr int standardInput = 0;
constexpr int standardOutput = 1;

unsigned int clampToCrtCount(std::size_t size)
{
    const auto largest = static_cast<std::size_t>(std::numeric_limits<int>::max());
    return static_cast<unsigned int>(std::min(size, largest));
}
} // namespace

void prepareStandardStreams()
{
    _setmode(standardInput, _O_BINARY);
    _setmode(standardOutput, _O_BINARY);
}

long readStandardInput(char* buffer, std::size_t size)
{
    return _read(standardInput, buffer, clampToCrtCount(size));
}

long writeStandardOutput(const char* bytes, std::size_t size)
{
    return _write(standardOutput, bytes, clampToCrtCount(size));
}
} // namespace term::bridge
