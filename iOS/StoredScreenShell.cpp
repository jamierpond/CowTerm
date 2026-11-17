#include "StoredScreenShell.h"

namespace cowterm::mobile
{
StoredScreenShell::StoredScreenShell(std::unique_ptr<term::Shell> innerToUse,
                                     std::string serializedStoredStateToUse)
    : inner(std::move(innerToUse))
    , serializedStoredState(std::move(serializedStoredStateToUse))
{
}

bool StoredScreenShell::start(const term::PtyOptions& options,
                              std::function<void(std::string)> onOutput,
                              std::function<void()> onExit)
{
    onRestoreState(serializedStoredState);

    inner->onRestoreState = onRestoreState;
    inner->onStatusChanged = onStatusChanged;
    return inner->start(options, std::move(onOutput), std::move(onExit));
}
} // namespace cowterm::mobile
