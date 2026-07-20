#pragma once

#include <string>

namespace term
{
struct WorktreeResult
{
    bool ok = false;
    std::string path;     // absolute path of the created worktree (on success)
    std::string message;  // git's own diagnostic (on failure)
};

// Creates a git worktree checked out to a brand-new branch based on the repo's
// mainline — the remote's default branch, else a local main/master, else HEAD —
// so a worktree is cut from main rather than from whatever the source row had
// checked out. The worktree lands beside the repo as "<repo>-<branch>": a plain
// sibling directory (slashes in the branch collapse to '-' for the directory
// leaf; the branch itself keeps its real name). git runs synchronously through
// the login shell, so the user's PATH and git apply, and the exact git error
// (not a repo, branch exists, target exists) is surfaced verbatim on failure.
WorktreeResult createWorktree(const std::string& repoPath, const std::string& branch);

// Moves an existing worktree checkout to the OS trash / recycle bin, then
// prunes git's now-stale registration in the parent repo. git itself confirms
// the path is a *linked* worktree (not a plain folder, and not a repo's main
// working tree) before anything is binned, and names the repo to prune. The
// branch itself is left alone. ok=false carries the OS/git diagnostic.
WorktreeResult removeWorktree(const std::string& worktreePath);

// True if path is a linked git worktree — a real checkout that is not the
// repo's main working tree, i.e. a valid target for removeWorktree. Shells out
// to git, so call it on a user action (a keypress), not in a render loop.
bool isWorktree(const std::string& path);
} // namespace term
