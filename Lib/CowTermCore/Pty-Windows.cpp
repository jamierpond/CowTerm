#include "Pty.h"

#include "DevShellEnvironmentCache.h"

#include <eacp/Core/Utils/WinInclude.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Strings.h>

#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace term
{
namespace
{
using eacp::Strings::narrow;
using eacp::Strings::widen;

std::wstring envVariable(const wchar_t* name)
{
    wchar_t buffer[MAX_PATH] = {};
    const auto length = GetEnvironmentVariableW(name, buffer, MAX_PATH);

    if (length == 0 || length >= MAX_PATH)
        return {};

    return {buffer, length};
}

std::wstring findShell()
{
    wchar_t path[MAX_PATH] = {};

    for (const auto* candidate: {L"pwsh.exe", L"powershell.exe"})
        if (SearchPathW(nullptr, candidate, nullptr, MAX_PATH, path, nullptr) > 0)
            return path;

    if (auto comspec = envVariable(L"ComSpec"); !comspec.empty())
        return comspec;

    return L"cmd.exe";
}

bool isVariableNamed(const std::wstring& entry, const std::wstring& name)
{
    return entry.size() > name.size() && entry[name.size()] == L'='
           && _wcsnicmp(entry.c_str(), name.c_str(), name.size()) == 0;
}

// A CreateProcessW environment block: this process's variables with `extras`
// set over them, each NUL-terminated, the block ending in one more NUL.
std::wstring environmentBlockWith(const std::vector<EnvironmentVariable>& extras)
{
    auto names = std::vector<std::wstring> {};

    for (const auto& variable: extras)
        names.push_back(widen(variable.name));

    const auto isOverridden = [&](const std::wstring& entry)
    {
        return std::any_of(names.begin(),
                           names.end(),
                           [&](const std::wstring& name)
                           { return isVariableNamed(entry, name); });
    };

    auto block = std::wstring {};

    if (auto* inherited = GetEnvironmentStringsW())
    {
        for (auto* entry = inherited; *entry != L'\0'; entry += wcslen(entry) + 1)
        {
            const auto line = std::wstring {entry};

            if (isOverridden(line))
                continue;

            block += line;
            block += L'\0';
        }

        FreeEnvironmentStringsW(inherited);
    }

    for (const auto& variable: extras)
    {
        block += widen(variable.name) + L"=" + widen(variable.value);
        block += L'\0';
    }

    block += L'\0';
    return block;
}

bool isDirectory(const std::wstring& path)
{
    const auto attributes = GetFileAttributesW(path.c_str());

    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring startDirectory(const std::string& requested)
{
    if (auto wide = widen(requested); !wide.empty() && isDirectory(wide))
        return wide;

    return envVariable(L"USERPROFILE");
}

COORD toCoord(const PtySize& size)
{
    return {(SHORT) size.cols, (SHORT) size.rows};
}

ULONGLONG creationTime(DWORD pid)
{
    auto* handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (handle == nullptr)
        return 0;

    FILETIME created {}, exited {}, kernel {}, user {};
    auto time = ULONGLONG {0};

    if (GetProcessTimes(handle, &created, &exited, &kernel, &user))
        time = ((ULONGLONG) created.dwHighDateTime << 32) | created.dwLowDateTime;

    CloseHandle(handle);
    return time;
}

// Base64 of an arbitrary byte buffer (no line wrapping). Small and dependency
// free so we don't have to pull in crypt32 just to build -EncodedCommand.
std::wstring base64(const std::vector<unsigned char>& bytes)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    constexpr auto bytesPerGroup = std::size_t {3};
    constexpr auto charactersPerGroup = std::size_t {4};
    constexpr auto padding = L'=';

    const auto getByteOrZero = [&](std::size_t index) -> unsigned char
    {
        if (index < bytes.size())
            return bytes[index];

        return 0;
    };

    const auto getCharacterOrPadding = [&](std::size_t byteIndex, unsigned sextet)
    {
        if (byteIndex < bytes.size())
            return (wchar_t) table[sextet];

        return padding;
    };

    std::wstring out;
    out.reserve((bytes.size() + bytesPerGroup - 1) / bytesPerGroup
                * charactersPerGroup);

    for (std::size_t i = 0; i < bytes.size(); i += bytesPerGroup)
    {
        const auto b0 = bytes[i];
        const auto b1 = getByteOrZero(i + 1);
        const auto b2 = getByteOrZero(i + 2);

        out.push_back(table[b0 >> 2]);
        out.push_back(table[((b0 & 0x03) << 4) | (b1 >> 4)]);
        out.push_back(getCharacterOrPadding(i + 1, ((b1 & 0x0f) << 2) | (b2 >> 6)));
        out.push_back(getCharacterOrPadding(i + 2, b2 & 0x3f));
    }

    return out;
}

// Base64 of the UTF-16LE encoding of an ASCII script — the exact form
// PowerShell's -EncodedCommand expects, which sidesteps all command-line
// quoting for a multi-line script.
std::wstring encodePowerShell(const std::string& script)
{
    std::vector<unsigned char> utf16le;
    utf16le.reserve(script.size() * 2);

    for (const auto c: script)
    {
        utf16le.push_back((unsigned char) c);
        utf16le.push_back(0);
    }

    return base64(utf16le);
}

// Runs a console program with no window and returns what it wrote to stdout.
std::string readOutputOf(const std::wstring& executable, std::wstring commandLine)
{
    auto inheritable = SECURITY_ATTRIBUTES {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    auto readEnd = HANDLE {};
    auto writeEnd = HANDLE {};

    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
        return {};

    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    auto startup = STARTUPINFOW {};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writeEnd;

    auto info = PROCESS_INFORMATION {};
    const auto launched = CreateProcessW(executable.c_str(),
                                         commandLine.data(),
                                         nullptr,
                                         nullptr,
                                         TRUE,
                                         CREATE_NO_WINDOW,
                                         nullptr,
                                         nullptr,
                                         &startup,
                                         &info);
    CloseHandle(writeEnd);

    auto output = std::string {};

    if (launched)
    {
        char buffer[4096];
        auto bytesRead = DWORD {0};

        while (ReadFile(readEnd, buffer, sizeof buffer, &bytesRead, nullptr)
               && bytesRead > 0)
        {
            output.append(buffer, bytesRead);
        }

        WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
    }

    CloseHandle(readEnd);
    return output;
}

// The architecture name Enter-VsDevShell's -arch takes, for this machine.
std::string getDevShellArchitecture()
{
    const auto processor = narrow(envVariable(L"PROCESSOR_ARCHITECTURE"));

    if (processor == "ARM64")
        return "arm64";

    if (processor == "x86")
        return "x86";

    return "x64";
}

std::optional<VisualStudioInstall> findVisualStudioInstall()
{
    const auto vswhere = envVariable(L"ProgramFiles(x86)")
                         + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";

    if (GetFileAttributesW(vswhere.c_str()) == INVALID_FILE_ATTRIBUTES)
        return std::nullopt;

    const auto output = readOutputOf(
        vswhere, L"\"" + vswhere + L"\" -latest -prerelease -products * -nologo");

    return parseVswhereOutput(output, getDevShellArchitecture());
}

std::string quotedForPowerShell(const std::string& text)
{
    auto quoted = std::string {"'"};

    for (const auto c: text)
    {
        quoted += c;

        if (c == '\'')
            quoted += c;
    }

    return quoted + "'";
}

// Runs Enter-VsDevShell in a throwaway PowerShell and returns the variables it
// added or changed. Empty when the install has no DevShell module or it fails.
std::vector<EnvironmentVariable> captureDevShellVariables(const std::wstring& shell,
                                                          const VisualStudioInstall& install)
{
    const auto path = quotedForPowerShell(install.installationPath);
    const auto arch = install.architecture;

    const auto script =
        "$ErrorActionPreference = 'Stop';"
        "[Console]::OutputEncoding = [Text.Encoding]::UTF8;"
        "$before = @{};"
        "Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value };"
        "Import-Module (Join-Path " + path
        + " 'Common7\\Tools\\Microsoft.VisualStudio.DevShell.dll');"
          "Enter-VsDevShell -VsInstallPath " + path
        + " -SkipAutomaticLocation -DevCmdArguments '-arch=" + arch + " -host_arch="
        + arch + "' | Out-Null;"
                 "Get-ChildItem env: | Where-Object { $before[$_.Name] -cne $_.Value } |"
                 " ForEach-Object { [Console]::Out.Write($_.Name + '=' + $_.Value + \"`n\") }";

    const auto output =
        readOutputOf(shell,
                     L"\"" + shell + L"\" -NoLogo -NoProfile -NonInteractive -EncodedCommand "
                         + encodePowerShell(script));

    return parseEnvironmentVariableLines(output);
}

std::filesystem::path getDevShellCacheFile()
{
    return std::filesystem::path {envVariable(L"LOCALAPPDATA")} / L"CowTerm"
           / L"devshell-environment.txt";
}

std::string readTextFile(const std::filesystem::path& file)
{
    auto stream = std::ifstream {file, std::ios::binary};
    return {std::istreambuf_iterator<char> {stream}, {}};
}

// Written aside and renamed over, so a pane starting meanwhile reads either
// the old cache or the new one, never half of one.
void writeTextFileWhole(const std::filesystem::path& file, const std::string& text)
{
    auto error = std::error_code {};
    std::filesystem::create_directories(file.parent_path(), error);

    auto temporary = file;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";

    {
        auto stream = std::ofstream {temporary, std::ios::binary};
        stream << text;
    }

    std::filesystem::rename(temporary, file, error);
}

// The Visual Studio developer environment for an interactive PowerShell: read
// from the cache when it was captured from the same install, otherwise
// captured now (about 1.5 s) and cached. COWTERM_NO_DEVSHELL turns it off.
std::vector<EnvironmentVariable> loadDevShellVariables(const std::wstring& shell)
{
    if (!envVariable(L"COWTERM_NO_DEVSHELL").empty())
        return {};

    const auto install = findVisualStudioInstall();

    if (!install)
        return {};

    const auto cacheFile = getDevShellCacheFile();
    const auto cached = parseDevShellEnvironmentCache(readTextFile(cacheFile));

    if (cached && isDevShellEnvironmentCacheFor(*cached, *install))
        return cached->variables;

    const auto captured =
        DevShellEnvironmentCache {*install, captureDevShellVariables(shell, *install)};

    if (!captured.variables.empty())
        writeTextFileWhole(cacheFile, serialiseDevShellEnvironmentCache(captured));

    return captured.variables;
}

// `base` without the variables `overrides` sets, then `overrides`.
std::vector<EnvironmentVariable>
    withOverrides(std::vector<EnvironmentVariable> base,
                  const std::vector<EnvironmentVariable>& overrides)
{
    const auto isOverridden = [&](const EnvironmentVariable& variable)
    {
        return std::any_of(overrides.begin(),
                           overrides.end(),
                           [&](const EnvironmentVariable& overriding)
                           { return _stricmp(overriding.name.c_str(), variable.name.c_str()) == 0; });
    };

    base.erase(std::remove_if(base.begin(), base.end(), isOverridden), base.end());
    base.insert(base.end(), overrides.begin(), overrides.end());
    return base;
}

struct ShellLaunch
{
    std::wstring commandLine;
    std::vector<EnvironmentVariable> variables;
};

// The shell's command line and the variables set over the inherited ones: a
// command runs through the shell; an interactive PowerShell starts with the
// Visual Studio developer environment already set, then loads its profile.
ShellLaunch prepareShellLaunch(const std::wstring& shell, const PtyOptions& options)
{
    const auto isCmd = shell.find(L"cmd.exe") != std::wstring::npos;
    auto launch = ShellLaunch {L"\"" + shell + L"\"", options.environment};

    if (!options.command.empty())
    {
        const auto wide = widen(options.command);
        launch.commandLine +=
            isCmd ? L" /c " + wide : L" -NoLogo -Command \"" + wide + L"\"";
    }
    else if (!isCmd)
    {
        launch.commandLine += L" -NoLogo";
        launch.variables =
            withOverrides(loadDevShellVariables(shell), options.environment);
    }

    return launch;
}

std::string baseName(const std::wstring& exeFile)
{
    auto name = narrow(exeFile);

    if (name.size() > 4)
    {
        auto extension = name.substr(name.size() - 4);
        std::transform(extension.begin(),
                       extension.end(),
                       extension.begin(),
                       [](unsigned char c) { return (char) std::tolower(c); });

        if (extension == ".exe")
            name.resize(name.size() - 4);
    }

    return name;
}

void closeHandles(std::initializer_list<HANDLE> handles)
{
    for (auto* handle: handles)
        CloseHandle(handle);
}

// Both pipe pairs plus the pseudoconsole on them. The console-side ends are
// still open here; launchShellProcess closes them once the child has started.
struct ConsolePipes
{
    HANDLE consoleInputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE consoleOutputWrite = nullptr;
    HPCON console = nullptr;
};

std::optional<ConsolePipes> createConsolePipes(const PtySize& size)
{
    auto pipes = ConsolePipes {};

    if (!CreatePipe(&pipes.consoleInputRead, &pipes.inputWrite, nullptr, 0))
        return std::nullopt;

    if (!CreatePipe(&pipes.outputRead, &pipes.consoleOutputWrite, nullptr, 0))
    {
        closeHandles({pipes.consoleInputRead, pipes.inputWrite});
        return std::nullopt;
    }

    if (FAILED(CreatePseudoConsole(toCoord(size),
                                   pipes.consoleInputRead,
                                   pipes.consoleOutputWrite,
                                   0,
                                   &pipes.console)))
    {
        closeHandles({pipes.consoleInputRead,
                      pipes.inputWrite,
                      pipes.outputRead,
                      pipes.consoleOutputWrite});
        return std::nullopt;
    }

    return pipes;
}

bool createProcessOnConsole(HPCON console,
                            const PtyOptions& options,
                            PROCESS_INFORMATION& info)
{
    constexpr auto attributeCount = DWORD {1};

    auto listSize = SIZE_T {0};
    InitializeProcThreadAttributeList(nullptr, attributeCount, 0, &listSize);

    auto listStorage = std::vector<std::byte>(listSize);
    auto* attributes = (LPPROC_THREAD_ATTRIBUTE_LIST) listStorage.data();

    auto startup = STARTUPINFOEXW {};
    startup.StartupInfo.cb = sizeof startup;

    // Null standard handles, explicitly. Left unset, a child inherits ours
    // whenever they are pipes or files rather than a console -- CowTerm started
    // with its output redirected, or under ctest -- and the shell then writes
    // there instead of into the pseudoconsole, leaving the pane blank.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributes;

    const auto shell = findShell();
    const auto launch = prepareShellLaunch(shell, options);
    auto commandLine = launch.commandLine;
    const auto directory = startDirectory(options.workingDirectory);
    auto environment = environmentBlockWith(launch.variables);
    const auto* directoryOrNull = directory.empty() ? nullptr : directory.c_str();

    const auto launched =
        InitializeProcThreadAttributeList(attributes, attributeCount, 0, &listSize)
        && UpdateProcThreadAttribute(attributes,
                                     0,
                                     PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                     console,
                                     sizeof console,
                                     nullptr,
                                     nullptr)
        && CreateProcessW(shell.c_str(),
                          commandLine.data(),
                          nullptr,
                          nullptr,
                          FALSE,
                          EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                          environment.data(),
                          directoryOrNull,
                          &startup.StartupInfo,
                          &info);

    DeleteProcThreadAttributeList(attributes);
    return launched;
}

std::optional<PROCESS_INFORMATION> launchShellProcess(const ConsolePipes& pipes,
                                                      const PtyOptions& options)
{
    auto info = PROCESS_INFORMATION {};
    const auto launched = createProcessOnConsole(pipes.console, options, info);

    // The pseudoconsole duplicated its ends of both pipes; ours can go.
    closeHandles({pipes.consoleInputRead, pipes.consoleOutputWrite});

    if (!launched)
    {
        ClosePseudoConsole(pipes.console);
        closeHandles({pipes.inputWrite, pipes.outputRead});
        return std::nullopt;
    }

    CloseHandle(info.hThread);
    return info;
}

void readOutputUntilClosed(HANDLE readHandle,
                           const std::function<void(std::string)>& output,
                           const std::function<void()>& exit)
{
    constexpr auto readBufferSize = DWORD {65536};
    char buffer[readBufferSize];

    while (true)
    {
        auto count = DWORD {0};

        if (!ReadFile(readHandle, buffer, readBufferSize, &count, nullptr))
            break;

        if (count == 0)
            break;

        output(std::string {buffer, count});
    }

    exit();
}
} // namespace

// ConPTY state. The pseudoconsole is guarded by consoleLock: the waiter thread
// closes it when the shell exits, which must not race resize() on the UI
// thread.
struct Pty::Native
{
    void closeConsole()
    {
        auto lock = std::lock_guard {consoleLock};

        if (console != nullptr)
        {
            ClosePseudoConsole(console);
            console = nullptr;
        }
    }

    HPCON console = nullptr;
    HANDLE process = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    std::thread reader;
    std::thread waiter;
    std::mutex consoleLock;
};

Pty::Pty()
    : native(std::make_unique<Native>())
{
}

Pty::~Pty()
{
    shutdown();
}

bool Pty::start(const PtyOptions& options,
                std::function<void(std::string)> onOutput,
                std::function<void()> onExit)
{
    if (native->process != nullptr)
        return false;

    const auto pipes = createConsolePipes(options.size);

    if (!pipes)
        return false;

    const auto info = launchShellProcess(*pipes, options);

    if (!info)
        return false;

    native->console = pipes->console;
    native->process = info->hProcess;
    native->inputWrite = pipes->inputWrite;
    native->outputRead = pipes->outputRead;

    native->reader = std::thread(
        [readHandle = pipes->outputRead,
         output = std::move(onOutput),
         exit = std::move(onExit)] { readOutputUntilClosed(readHandle, output, exit); });

    // ConPTY keeps the output pipe open until the pseudoconsole is closed, so
    // the reader would never see the shell exit on its own. Waiting on the
    // process and closing the console is what breaks the pipe and lets the
    // reader finish (delivering onExit).
    native->waiter = std::thread(
        [state = native.get(), processHandle = info->hProcess]
        {
            WaitForSingleObject(processHandle, INFINITE);
            state->closeConsole();
        });

    return true;
}

void Pty::write(std::string_view data)
{
    auto remaining = data;

    while (native->inputWrite != nullptr && !remaining.empty())
    {
        auto written = DWORD {0};

        if (!WriteFile(native->inputWrite,
                       remaining.data(),
                       (DWORD) remaining.size(),
                       &written,
                       nullptr))
            break;

        remaining.remove_prefix(written);
    }
}

void Pty::resize(const PtySize& size)
{
    auto lock = std::lock_guard {native->consoleLock};

    if (native->console != nullptr)
        ResizePseudoConsole(native->console, toCoord(size));
}

namespace
{
struct TreeInfo
{
    DWORD youngestPid = 0;
    std::wstring youngestName;
    std::wstring rootName;
};

struct ProcessCandidate
{
    DWORD pid;
    DWORD parent;
    std::wstring name;
};

struct ProcessSnapshot
{
    std::wstring rootName;
    std::vector<ProcessCandidate> others;
};

ProcessSnapshot takeProcessSnapshot(DWORD rootPid)
{
    auto result = ProcessSnapshot {};
    auto* snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    auto entry = PROCESSENTRY32W {};
    entry.dwSize = sizeof entry;

    for (auto hasEntry = Process32FirstW(snapshot, &entry) != 0; hasEntry;
         hasEntry = Process32NextW(snapshot, &entry) != 0)
    {
        if (entry.th32ProcessID == rootPid)
            result.rootName = entry.szExeFile;
        else
            result.others.push_back(
                {entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile});
    }

    CloseHandle(snapshot);
    return result;
}

bool isNewChildOf(const ProcessCandidate& candidate,
                  DWORD parent,
                  const std::vector<DWORD>& tree)
{
    return candidate.parent == parent
           && std::find(tree.begin(), tree.end(), candidate.pid) == tree.end();
}

// What the user sees running is the youngest process in the shell's
// descendant tree; the shell itself is the fallback.
TreeInfo inspectProcessTree(DWORD rootPid)
{
    auto snapshot = takeProcessSnapshot(rootPid);
    auto info = TreeInfo {};
    info.rootName = snapshot.rootName;

    auto tree = std::vector<DWORD> {rootPid};
    auto bestTime = ULONGLONG {0};

    const auto considerCandidate = [&](const ProcessCandidate& candidate)
    {
        tree.push_back(candidate.pid);

        if (const auto time = creationTime(candidate.pid); time >= bestTime)
        {
            bestTime = time;
            info.youngestPid = candidate.pid;
            info.youngestName = candidate.name;
        }
    };

    for (auto index = std::size_t {0}; index < tree.size(); ++index)
    {
        for (const auto& candidate: snapshot.others)
        {
            if (isNewChildOf(candidate, tree[index], tree))
                considerCandidate(candidate);
        }
    }

    return info;
}

using NtQueryInformationProcessFn =
    NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

NtQueryInformationProcessFn findNtQueryInformationProcess()
{
    static const auto query = (NtQueryInformationProcessFn) (void*) GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");

    return query;
}

struct Curdir
{
    UNICODE_STRING DosPath;
    HANDLE Handle;
};

struct ParametersPrefix
{
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;
    Curdir CurrentDirectory;
};

bool readProcessMemoryExactly(HANDLE process,
                              const void* address,
                              void* into,
                              std::size_t bytes)
{
    auto copied = SIZE_T {0};
    return ReadProcessMemory(process, address, into, bytes, &copied) != 0
           && copied == bytes;
}

bool hasCurrentDirectory(const ParametersPrefix& parameters)
{
    const auto& dosPath = parameters.CurrentDirectory.DosPath;
    return dosPath.Buffer != nullptr && dosPath.Length > 0;
}

std::optional<ParametersPrefix> readProcessParameters(HANDLE process)
{
    auto info = PROCESS_BASIC_INFORMATION {};
    auto peb = PEB {};
    auto parameters = ParametersPrefix {};
    const auto query = findNtQueryInformationProcess();

    if (query(process, ProcessBasicInformation, &info, sizeof info, nullptr) != 0)
        return std::nullopt;

    if (!readProcessMemoryExactly(process, info.PebBaseAddress, &peb, sizeof peb))
        return std::nullopt;

    if (!readProcessMemoryExactly(
            process, peb.ProcessParameters, &parameters, sizeof parameters))
        return std::nullopt;

    return parameters;
}

std::string toForwardSlashDirectory(std::wstring path)
{
    constexpr auto driveRootLength = std::size_t {3};

    while (path.size() > driveRootLength && path.back() == L'\\')
        path.pop_back();

    // With '/' separators, as eacp's FilePath writes them (and so
    // FilePath::homeDirectory(), which the cwd is compared against).
    return eacp::FilePath::fromWide(path).str();
}

std::string readCurrentDirectory(HANDLE process)
{
    const auto parameters = readProcessParameters(process);

    if (!parameters || !hasCurrentDirectory(*parameters))
        return {};

    const auto& dosPath = parameters->CurrentDirectory.DosPath;
    auto path = std::wstring(dosPath.Length / sizeof(wchar_t), L'\0');

    if (!readProcessMemoryExactly(process, dosPath.Buffer, path.data(), dosPath.Length))
        return {};

    return toForwardSlashDirectory(std::move(path));
}

// The live working directory of another process, read from its PEB. The
// layout matches 64-bit Windows (x64 and ARM64): RTL_USER_PROCESS_PARAMETERS
// keeps CurrentDirectory right after the standard handles.
std::string processWorkingDirectory(DWORD pid)
{
    if (findNtQueryInformationProcess() == nullptr)
        return {};

    auto* handle =
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

    if (handle == nullptr)
        return {};

    auto result = readCurrentDirectory(handle);
    CloseHandle(handle);
    return result;
}
} // namespace

std::string Pty::foregroundProcess() const
{
    if (native->process == nullptr)
        return {};

    const auto info = inspectProcessTree(GetProcessId(native->process));
    return baseName(info.youngestName.empty() ? info.rootName : info.youngestName);
}

std::string Pty::currentWorkingDirectory() const
{
    if (native->process == nullptr)
        return {};

    const auto rootPid = GetProcessId(native->process);
    const auto info = inspectProcessTree(rootPid);

    // The youngest descendant has the truthful cwd (PowerShell keeps its
    // process directory pinned at its start dir); the shell is the fallback.
    if (info.youngestPid != 0)
        if (auto dir = processWorkingDirectory(info.youngestPid); !dir.empty())
            return dir;

    return processWorkingDirectory(rootPid);
}

bool Pty::isRunning() const
{
    return native->process != nullptr
           && WaitForSingleObject(native->process, 0) == WAIT_TIMEOUT;
}

void Pty::shutdown()
{
    // Closing the pseudoconsole ends the console session, which terminates
    // the attached shell the way closing a console window would.
    native->closeConsole();

    if (native->process != nullptr
        && WaitForSingleObject(native->process, 2000) == WAIT_TIMEOUT)
        TerminateProcess(native->process, 1);

    if (native->waiter.joinable())
        native->waiter.join();

    if (native->reader.joinable())
        native->reader.join();

    for (auto* handle: {&native->process, &native->inputWrite, &native->outputRead})
    {
        if (*handle != nullptr)
        {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
}
} // namespace term
