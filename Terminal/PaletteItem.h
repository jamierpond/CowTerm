#pragma once

#include <cstdint>
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
    std::int64_t lastUsed = 0;
    TermSession* session = nullptr;
};
} // namespace term
