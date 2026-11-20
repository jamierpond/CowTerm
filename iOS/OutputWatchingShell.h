#pragma once

#include "Shell/Shell.h"

#include <atomic>
#include <memory>

namespace cowterm::mobile
{
// Set from the shell's output thread whenever output arrives; the pane screen
// takes it on its own timer.
struct OutputArrivedFlag
{
    std::atomic<bool> hasArrived {false};
};

// Wraps the pane's shell and raises the flag on every piece of output, so the
// screen knows when to look for a Claude prompt again and that the pane is
// producing output. Everything passes through unchanged.
class OutputWatchingShell final : public term::Shell
{
public:
    OutputWatchingShell(std::unique_ptr<term::Shell> innerToUse,
                        std::shared_ptr<OutputArrivedFlag> flagToUse);

    bool start(const term::PtyOptions& options,
               std::function<void(std::string)> onOutput,
               std::function<void()> onExit) override;

    void write(std::string_view data) override { inner->write(data); }
    void writeUserInput(std::string_view data) override { inner->writeUserInput(data); }
    void resize(const term::PtySize& size) override { inner->resize(size); }

    std::string foregroundProcess() const override { return inner->foregroundProcess(); }

    std::string currentWorkingDirectory() const override
    {
        return inner->currentWorkingDirectory();
    }

    void terminate() override { inner->terminate(); }
    void detach() override { inner->detach(); }
    void takeSizeBack(const term::PtySize& size) override { inner->takeSizeBack(size); }

private:
    std::unique_ptr<term::Shell> inner;
    std::shared_ptr<OutputArrivedFlag> flag;
};
} // namespace cowterm::mobile
