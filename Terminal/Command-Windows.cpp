#include "Command.h"

#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <shellapi.h>

#include <filesystem>
#include <vector>

namespace term
{
namespace
{
using eacp::Strings::widen;

namespace fs = std::filesystem;

bool isFile(const fs::path& path)
{
    auto error = std::error_code {};
    return fs::is_regular_file(path, error);
}

// The callers write POSIX sh -- shellQuote, `2>/dev/null`, multi-line scripts
// with $variables -- and the git and gh they run come with Git for Windows,
// which also ships the bash that understands it. Found next to git rather than
// by name, because the bash.exe on PATH is as likely to be WSL's, which runs
// in a different filesystem. Empty when there is no Git for Windows.
fs::path gitBash()
{
    auto found = std::wstring(MAX_PATH, L'\0');

    if (SearchPathW(nullptr, L"git.exe", nullptr, MAX_PATH, found.data(), nullptr)
        > 0)
    {
        found.resize(wcslen(found.c_str()));

        // git.exe sits in <root>\cmd, <root>\bin or <root>\mingw64\bin.
        for (auto dir = fs::path {found}.parent_path(); dir.has_relative_path();
             dir = dir.parent_path())
            if (auto bash = dir / L"bin" / L"bash.exe"; isFile(bash))
                return bash;
    }

    auto programFiles = std::wstring(MAX_PATH, L'\0');
    const auto length =
        GetEnvironmentVariableW(L"ProgramFiles", programFiles.data(), MAX_PATH);

    if (length > 0 && length < MAX_PATH)
    {
        programFiles.resize(length);

        if (auto bash = fs::path {programFiles} / L"Git" / L"bin" / L"bash.exe";
            isFile(bash))
            return bash;
    }

    return {};
}

const fs::path& shell()
{
    static const auto path = gitBash();
    return path;
}

// One argument as CommandLineToArgvW (and the MSYS runtime, which follows it)
// reads it back: quoted, with each run of backslashes doubled where it ends at
// a quote.
std::wstring quoteArgument(const std::wstring& argument)
{
    auto out = std::wstring {L"\""};
    auto backslashes = std::size_t {0};

    for (const auto c: argument)
    {
        if (c == L'\\')
        {
            ++backslashes;
            continue;
        }

        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        out += c;
        backslashes = 0;
    }

    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

std::wstring commandLineFor(const std::string& command)
{
    if (!shell().empty())
        return quoteArgument(shell().wstring()) + L" -lc "
               + quoteArgument(widen(command));

    // No POSIX shell to hand: the command gets cmd's reading of it, which is
    // enough for a plain `git ...` and nothing more.
    return L"cmd.exe /d /s /c \"" + widen(command) + L"\"";
}

struct Handle
{
    ~Handle()
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    HANDLE handle = nullptr;
};
} // namespace

// CreateProcess rather than _popen: _popen starts cmd.exe with a console of its
// own, and from a GUI app every one of those flashes a window on screen.
CommandOutput runViaLoginShell(const std::string& command)
{
    auto inheritable = SECURITY_ATTRIBUTES {};
    inheritable.nLength = sizeof inheritable;
    inheritable.bInheritHandle = TRUE;

    auto outputRead = Handle {};
    auto outputWrite = Handle {};

    if (!CreatePipe(&outputRead.handle, &outputWrite.handle, &inheritable, 0))
        return {-1, "could not launch command"};

    SetHandleInformation(outputRead.handle, HANDLE_FLAG_INHERIT, 0);

    auto input = Handle {CreateFileW(L"NUL",
                                     GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &inheritable,
                                     OPEN_EXISTING,
                                     0,
                                     nullptr)};

    auto startup = STARTUPINFOW {};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.handle;
    startup.hStdOutput = outputWrite.handle;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    auto commandLine = commandLineFor(command);
    auto info = PROCESS_INFORMATION {};

    if (!CreateProcessW(nullptr,
                        commandLine.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startup,
                        &info))
        return {-1, "could not launch command"};

    auto process = Handle {info.hProcess};
    CloseHandle(info.hThread);

    // Our copy of the write end has to go, or the read below never sees the
    // end of the output.
    CloseHandle(outputWrite.handle);
    outputWrite.handle = nullptr;

    auto text = std::string {};
    auto buffer = std::vector<char>(4096);
    auto count = DWORD {0};

    while (
        ReadFile(
            outputRead.handle, buffer.data(), (DWORD) buffer.size(), &count, nullptr)
        && count > 0)
        text.append(buffer.data(), count);

    WaitForSingleObject(process.handle, INFINITE);

    auto exitCode = DWORD {0};
    const auto status =
        GetExitCodeProcess(process.handle, &exitCode) ? (int) exitCode : -1;

    return {status, std::move(text)};
}

void openInBrowser(const std::string& url)
{
    ShellExecuteW(
        nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
} // namespace term
