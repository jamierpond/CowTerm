#pragma once

#include <string>

namespace term
{
// Moves a file or directory to the OS trash / recycle bin — an undoable
// delete, not a permanent unlink, so the user can recover it from Finder /
// Explorer. Returns true on success; on failure returns false and fills
// `error` with a platform diagnostic.
bool moveToTrash(const std::string& path, std::string& error);
} // namespace term
