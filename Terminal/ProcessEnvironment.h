#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace term
{
// One variable from another process's environment as it started, if the
// operating system lets this user read it.
std::optional<std::string> readProcessEnvironmentVariable(std::int64_t processId,
                                                          const std::string& name);
} // namespace term
