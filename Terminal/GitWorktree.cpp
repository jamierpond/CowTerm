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

// Runs a shell command through the user's login shell — the same trick the
// lazygit popup uses — so a GUI launch (with its stripped PATH) still finds
// their git. On Windows there's no login shell to wrap, so run it directly.
CommandOutput runViaLoginShell(const std::string& command)
{
#if defined(_WIN32)
    return runCaptured(command);
#else
    const auto* shell = std::getenv("SHELL");
    const auto wrapped =
        shellQuote(shell != nullptr ? shell : "/bin/sh") + " -lc " + shellQuote(command);
    return runCaptured(wrapped);
#endif
}

// The start-point a new worktree branches from: the repo's mainline, resolved
// as the remote's default branch, else a local main/master, else HEAD. The
// grouped `||` fallbacks each print exactly one ref (or nothing) and stop at
// the first that resolves.
std::string resolveBaseBranch(const std::string& repoPath)
{
    const auto q = shellQuote(repoPath);
    const auto script =
        "git -C " + q + " symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null"
        " || { git -C " + q + " show-ref --verify --quiet refs/heads/main && echo main; }"
        " || { git -C " + q + " show-ref --verify --quiet refs/heads/master && echo master; }"
        " || echo HEAD";

    const auto ref = trimmed(runViaLoginShell(script).text);
    return ref.empty() ? std::string {"HEAD"} : ref;
}

struct WorktreeProbe
{
    bool linked = false;   // path is a git worktree that is NOT the main working tree
    std::string repoPath;  // owning repo's main working tree (set when linked)
};

// Ask git what `path` is. A checkout's private git dir (--absolute-git-dir)
// equals the shared common dir (--git-common-dir) for a repo's main working
// tree and differs for a linked worktree; that difference is what marks a
// worktree we may trash. The common dir, "<repo>/.git", also names the repo to
// prune. Both are absolute lines thanks to --path-format=absolute.
WorktreeProbe probeWorktree(const std::string& path)
{
    const auto probe = runViaLoginShell(
        "git -C " + shellQuote(path)
        + " rev-parse --path-format=absolute --absolute-git-dir --git-common-dir 2>/dev/null");

    if (probe.status != 0)
        return {};

    const auto text = probe.text;
    const auto newline = text.find('\n');

    if (newline == std::string::npos)
        return {};

    const auto gitDir = trimmed(text.substr(0, newline));
    const auto commonDir = trimmed(text.substr(newline + 1));

    if (gitDir.empty() || commonDir.empty() || gitDir == commonDir)
        return {};  // main working tree, or unparseable — not a linked worktree

    // Strip the trailing "/.git" (or bare ".git") to get the repo's top level.
    const auto suffix = std::string {"/.git"};
    auto repo = commonDir;

    if (repo.size() >= suffix.size() && repo.compare(repo.size() - suffix.size(), suffix.size(), suffix) == 0)
        repo = repo.substr(0, repo.size() - suffix.size());

    return {true, repo};
}
} // namespace

WorktreeResult createWorktree(const std::string& repoPath, const std::string& branchInput)
{
    const auto branch = trimmed(branchInput);

    if (branch.empty())
        return {false, {}, "branch name is empty"};

    // A plain sibling of the repo: ~/dir/repo -> ~/dir/repo-<leaf>.
    const auto path = repoPath + "-" + dirLeaf(branch);
    const auto base = resolveBaseBranch(repoPath);

    // git -C <repo> worktree add <path> -b <branch> <base>; the start-point
    // cuts the branch from mainline rather than the source row's HEAD. 2>&1
    // folds git's diagnostics into the captured stream so failures surface
    // verbatim.
    const auto git = "git -C " + shellQuote(repoPath) + " worktree add " + shellQuote(path)
                     + " -b " + shellQuote(branch) + " " + shellQuote(base) + " 2>&1";

    const auto result = runViaLoginShell(git);

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

    // Guard: only ever bin a genuine linked worktree. git — not a path string —
    // confirms this is one and names the repo to prune, so a plain folder or a
    // repo's main working tree is refused rather than trashed.
    const auto probe = probeWorktree(path);

    if (!probe.linked)
        return {false, {}, "not a worktree directory"};

    // Trash the checkout first; if that fails there's nothing to prune.
    auto trashError = std::string {};

    if (!moveToTrash(path, trashError))
        return {false, {}, trashError.empty() ? "could not move to Trash" : trashError};

    // Pruning clears git's now-dangling registration for the folder we just
    // binned. Best-effort: the checkout is already gone, so a prune hiccup
    // shouldn't read as a failed removal.
    runViaLoginShell("git -C " + shellQuote(probe.repoPath) + " worktree prune 2>&1");

    return {true, path, {}};
}

bool isWorktree(const std::string& path)
{
    return probeWorktree(trimmed(path)).linked;
}
} // namespace term
