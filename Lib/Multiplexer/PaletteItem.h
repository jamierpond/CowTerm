#pragma once

#include "UnreadMark.h"

#include <cstdint>
#include <optional>
#include <string>

namespace term
{
class TermSession;

struct PaletteItem
{
    enum class Kind
    {
        Session,
        Project
    };

    Kind kind = Kind::Project;
    std::string key;
    std::string label;
    std::string detail;
    std::string status;
    bool claude = false;

    // The remote machine the row lives on; empty for this one.
    std::string host;

    // A remote host that can't be reached: shown, greyed, never opened.
    bool unavailable = false;
    std::int64_t lastUsed = 0;

    // An open session with a pane Jamie has not seen.
    std::optional<UnreadMark> unread;
    TermSession* session = nullptr;
};

// The mark before a row's name: ✳ for Claude, ● for a session, ▸ for a project.
std::string getPaletteItemIcon(const PaletteItem& item);
} // namespace term
