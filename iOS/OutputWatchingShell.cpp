#include "OutputWatchingShell.h"

namespace cowterm::mobile
{
OutputWatchingShell::OutputWatchingShell(std::unique_ptr<term::Shell> innerToUse,
                                         std::shared_ptr<OutputArrivedFlag> flagToUse)
    : inner(std::move(innerToUse))
    , flag(std::move(flagToUse))
{
}

bool OutputWatchingShell::start(const term::PtyOptions& options,
                                std::function<void(std::string)> onOutput,
                                std::function<void()> onExit)
{
    inner->onRestoreState = [this](const std::string& state)
    {
        flag->hasArrived = true;
        onRestoreState(state);
    };
    inner->onStatusChanged = onStatusChanged;

    auto watchedOutput = [flag = flag, output = std::move(onOutput)](std::string bytes)
    {
        flag->hasArrived = true;
        output(std::move(bytes));
    };

    return inner->start(options, std::move(watchedOutput), std::move(onExit));
}
} // namespace cowterm::mobile
