#include "ProcessEnvironment.h"

namespace term
{
// Reading another process's environment needs its PEB; the board falls back
// to matching panes by working directory instead.
std::optional<std::string> readProcessEnvironmentVariable(std::int64_t, const std::string&)
{
    return std::nullopt;
}
} // namespace term
