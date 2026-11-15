// ct on Windows: CowTerm.exe is a GUI executable, so it starts without a
// console; ct borrows the console of the shell that ran it.

#include "CliPlatform.h"

#include <eacp/Core/Utils/WinInclude.h>

#include <cstdio>

namespace term::cli::platform
{
namespace
{
bool hasHandle(DWORD which)
{
    auto* handle = GetStdHandle(which);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE
           && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}
} // namespace

void attachToTerminal()
{
    const auto needsOutput = !hasHandle(STD_OUTPUT_HANDLE);
    const auto needsError = !hasHandle(STD_ERROR_HANDLE);

    if (!needsOutput && !needsError)
        return;

    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    auto* stream = (FILE*) nullptr;

    if (needsOutput)
        freopen_s(&stream, "CONOUT$", "w", stdout);

    if (needsError)
        freopen_s(&stream, "CONOUT$", "w", stderr);
}
} // namespace term::cli::platform
