#include "MacProcesses.h"

#include <fcntl.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <climits>

extern char** environ;

namespace term::macos
{
namespace fs = std::filesystem;

namespace
{
constexpr auto readChunkSize = 4096;

std::string readUntilClosed(int fileDescriptor)
{
    auto text = std::string {};
    auto chunk = std::array<char, readChunkSize> {};

    for (auto count = read(fileDescriptor, chunk.data(), chunk.size()); count > 0;
         count = read(fileDescriptor, chunk.data(), chunk.size()))
        text.append(chunk.data(), static_cast<std::size_t>(count));

    return text;
}

int waitForExitStatus(pid_t pid)
{
    auto status = 0;

    if (waitpid(pid, &status, 0) == pid && WIFEXITED(status))
        return WEXITSTATUS(status);

    return -1;
}
} // namespace

std::vector<char*> createArgumentVector(const std::vector<std::string>& args)
{
    auto argv = std::vector<char*> {};

    for (const auto& arg: args)
        argv.push_back(const_cast<char*>(arg.c_str()));

    argv.push_back(nullptr);
    return argv;
}

fs::path getExecutablePath()
{
    auto size = uint32_t {PATH_MAX};
    auto buffer = std::vector<char>(size);

    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        buffer.resize(size);
        _NSGetExecutablePath(buffer.data(), &size);
    }

    auto error = std::error_code {};
    auto resolved = fs::canonical(buffer.data(), error);

    if (error)
        return fs::path {buffer.data()};

    return resolved;
}

std::optional<fs::path> findAppBundlePath()
{
    for (auto path = getExecutablePath(); path.has_parent_path() && path != path.root_path();
         path = path.parent_path())
    {
        if (path.extension() == ".app")
            return path;
    }

    return std::nullopt;
}

ProcessOutput runProcessCapturingOutput(const std::vector<std::string>& args,
                                        StandardInput input)
{
    auto result = ProcessOutput {};
    int fds[2];

    if (pipe(fds) != 0)
        return result;

    auto actions = posix_spawn_file_actions_t {};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);

    if (input == StandardInput::discarded)
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    auto argv = createArgumentVector(args);
    auto pid = pid_t {};
    const auto spawned = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (spawned == 0)
    {
        result.text = readUntilClosed(fds[0]);
        result.status = waitForExitStatus(pid);
    }

    close(fds[0]);
    return result;
}
} // namespace term::macos
