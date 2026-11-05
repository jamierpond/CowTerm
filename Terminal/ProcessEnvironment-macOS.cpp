#include "ProcessEnvironment.h"

#include <sys/sysctl.h>

#include <cstring>
#include <vector>

namespace term
{
std::optional<std::string> readProcessEnvironmentVariable(std::int64_t processId,
                                                          const std::string& name)
{
    auto maximumArgumentsSize = 0;
    auto sizeOfMaximum = sizeof(maximumArgumentsSize);
    int maximumQuery[] = {CTL_KERN, KERN_ARGMAX};

    if (sysctl(maximumQuery, 2, &maximumArgumentsSize, &sizeOfMaximum, nullptr, 0) != 0)
        return std::nullopt;

    auto buffer = std::vector<char>((std::size_t) maximumArgumentsSize);
    auto bufferSize = buffer.size();
    auto nativeProcessId = static_cast<int>(processId);
    int argumentsQuery[] = {CTL_KERN, KERN_PROCARGS2, nativeProcessId};

    if (sysctl(argumentsQuery, 3, buffer.data(), &bufferSize, nullptr, 0) != 0)
        return std::nullopt;

    // KERN_PROCARGS2: argc, the executable path, padding, argv, then the environment.
    auto argumentCount = 0;

    if (bufferSize < sizeof(argumentCount))
        return std::nullopt;

    std::memcpy(&argumentCount, buffer.data(), sizeof(argumentCount));
    auto position = sizeof(argumentCount);
    const auto end = bufferSize;

    auto skipString = [&]
    {
        while (position < end && buffer[position] != '\0')
            ++position;
    };

    auto skipNulls = [&]
    {
        while (position < end && buffer[position] == '\0')
            ++position;
    };

    skipString();
    skipNulls();

    for (auto argument = 0; argument < argumentCount && position < end; ++argument)
    {
        skipString();
        ++position;
    }

    const auto prefix = name + "=";

    while (position < end && buffer[position] != '\0')
    {
        const auto entry = std::string {buffer.data() + position};

        if (entry.starts_with(prefix))
            return entry.substr(prefix.size());

        position += entry.size() + 1;
    }

    return std::nullopt;
}
} // namespace term
