#pragma once

#include "Emulator/TerminalModel.h"
#include "Shell/Shell.h"

#include <memory>

namespace term::phone
{
// A copy of the shown pane's terminal, fed the same bytes and state as the
// pane, so the phone can store its last screen without reaching into the view.
struct PaneMirror
{
    Theme theme;
    TerminalModel model {defaultColumns, defaultRows, theme};

    // Output or a restored state reached the mirror: it shows the pane.
    bool hasContent = false;

    static constexpr int defaultColumns = 80;
    static constexpr int defaultRows = 24;
};

// Wraps the pane's shell: everything passes through unchanged, and the
// mirror sees the output, the restored state, every resize and every status
// note (link lost, size held elsewhere). Main-thread object, as daemon shells
// deliver on the main thread.
class MirroredShell final : public Shell
{
public:
    MirroredShell(std::unique_ptr<Shell> innerToUse,
                  std::shared_ptr<PaneMirror> mirrorToUse,
                  std::function<void(const std::string&)> onStatusNoteToUse);

    bool start(const PtyOptions& options,
               std::function<void(std::string)> onOutput,
               std::function<void()> onExit) override;

    void write(std::string_view data) override { inner->write(data); }
    void writeUserInput(std::string_view data) override { inner->writeUserInput(data); }
    void resize(const PtySize& size) override;

    std::string foregroundProcess() const override { return inner->foregroundProcess(); }
    std::string currentWorkingDirectory() const override { return inner->currentWorkingDirectory(); }

    void terminate() override { inner->terminate(); }
    void detach() override { inner->detach(); }

private:
    std::unique_ptr<Shell> inner;
    std::shared_ptr<PaneMirror> mirror;
    std::function<void(const std::string&)> onStatusNote;
};
} // namespace term::phone
