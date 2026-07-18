#include "Trash.h"

#include <windows.h>

#include <shellapi.h>

#include <string>
#include <vector>

namespace term
{
bool moveToTrash(const std::string& path, std::string& error)
{
    // Widen the UTF-8 path Windows-side (the app deals in UTF-8 throughout).
    const auto needed =
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);

    if (needed <= 0)
    {
        error = "could not decode path";
        return false;
    }

    // SHFileOperation wants a double-null-terminated list; MultiByteToWideChar
    // already writes one terminator, so pad a second.
    auto wide = std::vector<wchar_t>((std::size_t) needed + 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), needed);

    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = wide.data();
    // FOF_ALLOWUNDO is what routes the delete to the Recycle Bin rather than
    // erasing outright; the rest suppress the shell's own UI/confirmations.
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    const auto rc = SHFileOperationW(&op);

    if (rc != 0)
        error = "recycle failed (code " + std::to_string(rc) + ")";

    return rc == 0;
}
} // namespace term
