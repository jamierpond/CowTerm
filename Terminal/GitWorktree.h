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

// Creates a git worktree checked out to a brand-new branch based on the
// repo's current HEAD. The worktree lands in "<repo>.worktrees/<branch>" — a
// sibling folder that groups every worktree for the repo (slashes in the
// branch collapse to '-' for the directory leaf; the branch itself keeps its
// real name). git runs synchronously through the login shell, so the user's
// PATH and git apply, and the exact git error (not a repo, branch exists,
// dirty tree) is surfaced verbatim on failure.
WorktreeResult createWorktree(const std::string& repoPath, const std::string& branch);

// Moves an existing worktree checkout to the OS trash / recycle bin, then
// prunes git's now-stale registration in the parent repo. worktreePath is the
// absolute "<repo>.worktrees/<leaf>" directory createWorktree laid down; a
// path outside that layout is refused so this never bins arbitrary folders.
// The branch itself is left alone. ok=false carries the OS/git diagnostic.
WorktreeResult removeWorktree(const std::string& worktreePath);
} // namespace term
