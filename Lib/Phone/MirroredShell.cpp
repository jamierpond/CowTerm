#include "MirroredShell.h"

namespace term::phone
{
MirroredShell::MirroredShell(std::unique_ptr<Shell> innerToUse,
                             std::shared_ptr<PaneMirror> mirrorToUse,
                             std::function<void(const std::string&)> onStatusNoteToUse)
    : inner(std::move(innerToUse))
    , mirror(std::move(mirrorToUse))
    , onStatusNote(std::move(onStatusNoteToUse))
{
}

bool MirroredShell::start(const PtyOptions& options,
                          std::function<void(std::string)> onOutput,
                          std::function<void()> onExit)
{
    mirror->model.screen.resize(options.size.cols, options.size.rows);

    inner->onRestoreState = [shared = mirror, restore = onRestoreState](const std::string& serialized)
    {
        if (const auto state = parseTerminalState(serialized))
        {
            shared->model.restoreState(*state);
            shared->hasContent = true;
        }

        restore(serialized);
    };

    inner->onStatusChanged = [note = onStatusNote, show = onStatusChanged](const std::string& status)
    {
        note(status);
        show(status);
    };

    auto mirrorThenShow = [shared = mirror, show = std::move(onOutput)](std::string bytes)
    {
        shared->model.feed(bytes);
        shared->hasContent = true;
        shared->onOutput(bytes.size());
        show(std::move(bytes));
    };

    return inner->start(options, std::move(mirrorThenShow), std::move(onExit));
}

void MirroredShell::resize(const PtySize& size)
{
    mirror->model.screen.resize(size.cols, size.rows);
    inner->resize(size);
}
} // namespace term::phone
