#pragma once

#include "Shell/Shell.h"

#include <memory>
#include <string>

namespace cowterm::mobile
{
// Wraps the pane's shell so the pane draws the screen the last launch stored
// the moment it starts, before the link is up; the daemon's own state replaces
// it when it arrives. Everything else passes through unchanged.
class StoredScreenShell final : public term::Shell
{
public:
    StoredScreenShell(std::unique_ptr<term::Shell> innerToUse,
                      std::string serializedStoredStateToUse);

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
    std::string serializedStoredState;
};
} // namespace cowterm::mobile
