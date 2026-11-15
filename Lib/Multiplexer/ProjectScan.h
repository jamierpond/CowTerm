#pragma once

#include <string>
#include <vector>

namespace term
{
// Each root that is a directory, then its depth-1 subdirectories (hidden
// ones included, so dotfile repos like ~/.config are reachable), deduped,
// as generic paths. Roots must already have ~ expanded.
std::vector<std::string> listProjectDirectories(const std::vector<std::string>& roots);
} // namespace term
