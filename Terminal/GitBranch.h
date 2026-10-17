#pragma once

#include <string>
#include <unordered_map>

namespace term
{
// The branch checked out in the git repo (or worktree, or submodule) holding
// `dir`, read straight from its HEAD file: no git process, so it is cheap
// enough to ask on every Rudder push. Empty outside a repo and on a detached
// HEAD.
std::string gitBranch(const std::string& dir);

// gitBranch, remembering where each directory's HEAD file is so a lookup is
// one small read rather than a walk up the tree. HEAD itself is re-read every
// time, so a checkout shows on the next lookup.
class GitBranches
{
public:
    std::string of(const std::string& dir);

private:
    std::unordered_map<std::string, std::string> heads;
};
} // namespace term
