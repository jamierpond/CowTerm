#pragma once

#include <Miro/Reflect.h>

#include <string>
#include <vector>

namespace term
{
// Flat, reflectable snapshot of a pane tree; `first`/`second` index into the
// same vector, node 0 is the root. Leaves carry the shell's directory.
struct SavedPane
{
    bool split = false;
    bool horizontal = false;
    float ratio = 0.5f;
    std::string cwd;

    // Names the pane's shell in the session daemon, so a restore adopts
    // the still-running process instead of spawning a new one.
    std::string shellId;
    int first = -1;
    int second = -1;

    MIRO_REFLECT(split, horizontal, ratio, cwd, shellId, first, second)
};

struct SavedSession
{
    std::string name;
    std::string projectDir;
    std::string cwd;
    std::vector<SavedPane> panes;

    MIRO_REFLECT(name, projectDir, cwd, panes)
};

// The project directory a session is filed under on the Mac: home expanded,
// trailing slashes dropped.
std::string normalizedDir(const std::string& dir);

// The "layout" message that leaves session's layout with its host's daemon, so
// another Mac or the phone attaching there knows which panes form the session.
std::string makeLayoutMessage(const SavedSession& session);
} // namespace term
