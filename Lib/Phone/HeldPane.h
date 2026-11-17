#pragma once

#include "Shell/Pty.h"

#include <filesystem>
#include <optional>
#include <string>

namespace term::phone
{
// The pane the phone holds on a daemon from before size owners, with the size
// it had before the phone took it. Kept on disk while held, so a launch after
// the app was killed in the foreground (a reinstall, a crash), which gave
// nothing back, can still give the pane its size back.
struct HeldPane
{
    std::string host;
    std::string paneId;
    PtySize sizeBeforeThePhone;
};

// False when it could not be written.
bool saveHeldPane(const std::filesystem::path& path, const HeldPane& pane);

// Empty when there is no file, or it does not parse.
std::optional<HeldPane> loadHeldPane(const std::filesystem::path& path);

void forgetHeldPane(const std::filesystem::path& path);
} // namespace term::phone
