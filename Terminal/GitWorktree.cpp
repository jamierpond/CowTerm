#include "GitWorktree.h"

#include "Trash.h"

#include <array>
#include <cstdio>
#include <cstdlib>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace term
{
namespace
{
std::string trimmed(const std::string& text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");

    if (begin == std::string::npos)
        return {};

    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

// Single-quote for POSIX sh, escaping any embedded quote as '\''. Safe to
// nest: the result is a literal to one shell, which is exactly what the
// login-shell wrap below relies on.
std::string shellQuote(const std::string& text)
{
    auto out = std::string {"'"};

    for (const auto c: text)
        out += c == '\'' ? "'\\''" : std::string {c};

    out += "'";
    return out;
}

// A filesystem-friendly leaf for the worktree directory. The git branch keeps
// its real name (feature/foo); only the folder flattens ("feature-foo").
std::string dirLeaf(const std::string& branch)
{
    auto out = std::string {};

    for (const auto c: branch)
        out += (c == '/' || c == '\\' || c == ' ' || c == ':') ? '-' : c;

    return out;
}

struct CommandOutput
{
    int status = -1;
    std::string text;
};

CommandOutput runCaptured(const std::string& command)
{
#if defined(_WIN32)
    auto* pipe = _popen(command.c_str(), "r");
#else
    auto* pipe = popen(command.c_str(), "r");
#endif

    if (pipe == nullptr)
        return {-1, "could not launch git"};

    auto text = std::string {};
    auto buffer = std::array<char, 512> {};

    while (std::fgets(buffer.data(), (int) buffer.size(), pipe) != nullptr)
        text += buffer.data();

#if defined(_WIN32)
    const auto status = _pclose(pipe);
#else
    const auto raw = pclose(pipe);
    const auto status = (raw != -1 && WIFEXITED(raw)) ? WEXITSTATUS(raw) : -1;
#endif

    return {status, std::move(text)};
}
} // namespace

WorktreeResult createWorktree(const std::string& repoPath, const std::string& branchInput)
{
    const auto branch = trimmed(branchInput);

    if (branch.empty())
        return {false, {}, "branch name is empty"};

    const auto path = repoPath + ".worktrees/" + dirLeaf(branch);

    // git -C <repo> worktree add <path> -b <branch>; 2>&1 folds git's
    // diagnostics into the captured stream so a failure surfaces verbatim.
    const auto git = "git -C " + shellQuote(repoPath) + " worktree add "
                     + shellQuote(path) + " -b " + shellQuote(branch) + " 2>&1";

#if defined(_WIN32)
    const auto command = git;
#else
    // Run through the user's login shell — the same trick the lazygit popup
    // uses — so a GUI launch (with its stripped PATH) still finds their git.
    const auto* shell = std::getenv("SHELL");
    const auto command =
        shellQuote(shell != nullptr ? shell : "/bin/sh") + " -lc " + shellQuote(git);
#endif

    const auto result = runCaptured(command);

    if (result.status != 0)
    {
        const auto message = trimmed(result.text);
        return {false, {}, message.empty() ? "git worktree failed" : message};
    }

    return {true, path, {}};
}

WorktreeResult removeWorktree(const std::string& worktreePath)
{
    const auto path = trimmed(worktreePath);

    if (path.empty())
        return {false, {}, "no worktree path"};

    // Guard: only ever bin paths that live in a "<repo>.worktrees/" tree — the
    // exact layout createWorktree lays down. Anything else isn't a worktree we
    // created and mustn't be trashed by this path.
    const auto marker = std::string {".worktrees/"};
    const auto at = path.find(marker);

    if (at == std::string::npos)
        return {false, {}, "not a worktree directory"};

    // Trash the checkout first; if that fails there's nothing to prune.
    auto trashError = std::string {};

    if (!moveToTrash(path, trashError))
        return {false, {}, trashError.empty() ? "could not move to Trash" : trashError};

    // The parent repo is everything up to the ".worktrees/" segment. Pruning
    // clears git's now-dangling registration for the folder we just binned.
    // Best-effort: the checkout is already gone, so a prune hiccup shouldn't
    // read as a failed removal.
    const auto repoPath = path.substr(0, at);
    const auto git = "git -C " + shellQuote(repoPath) + " worktree prune 2>&1";

#if defined(_WIN32)
    const auto command = git;
#else
    const auto* shell = std::getenv("SHELL");
    const auto command =
        shellQuote(shell != nullptr ? shell : "/bin/sh") + " -lc " + shellQuote(git);
#endif

    runCaptured(command);

    return {true, path, {}};
}
} // namespace term
