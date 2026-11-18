#pragma once

#include "Multiplexer/Projects.h"
#include "Multiplexer/RemoteLocation.h"
#include "Multiplexer/SessionLayout.h"
#include "Shell/Shell.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// A new session for a project with none open, the second thing the phone
// starts (dv is the first): one login shell in the project's folder, named as
// the Mac names that project's session, started only from a tap on its row.
namespace term::phone
{
struct ProjectSession
{
    std::string host;
    std::string paneId;
    std::string folder;
    std::string sessionName;

    // The layout the phone publishes on the session's daemon, as a Mac
    // publishes its own: one pane, filed under the directory the Mac's palette
    // lists the project under, so the Mac adopts it as that project's session.
    SavedSession makeLayout() const
    {
        auto pane = SavedPane {};
        pane.cwd = folder;
        pane.shellId = paneId;
        return SavedSession {sessionName, remote::makeRemoteProjectDir(host, folder), folder, {pane}};
    }
};

// The session for folder on host, under a new pane id made from randomNumber.
// Empty unless host's last project scan listed folder.
std::optional<ProjectSession> makeProjectSession(const std::string& host,
                                                 const std::vector<ProjectDir>& lastScan,
                                                 const std::string& folder,
                                                 std::uint64_t randomNumber);

// Sixteen hex digits, the form of the Mac's own pane ids.
std::string makePaneIdFromRandomNumber(std::uint64_t randomNumber);

// The project pane's shell: it starts the login shell in the project's folder
// and nothing else. A view starting it with no folder gets the project's;
// start() with a command or another folder starts nothing and returns false.
class ProjectShell final : public Shell
{
public:
    ProjectShell(std::unique_ptr<Shell> innerToUse, ProjectSession sessionToUse);

    bool start(const PtyOptions& options,
               std::function<void(std::string)> onOutput,
               std::function<void()> onExit) override;

    void write(std::string_view data) override { inner->write(data); }
    void writeUserInput(std::string_view data) override { inner->writeUserInput(data); }
    void resize(const PtySize& size) override { inner->resize(size); }

    std::string foregroundProcess() const override { return inner->foregroundProcess(); }
    std::string currentWorkingDirectory() const override { return inner->currentWorkingDirectory(); }

    void terminate() override { inner->terminate(); }
    void detach() override { inner->detach(); }
    void takeSizeBack() override { inner->takeSizeBack(); }

    // After the login shell's spawn went to the daemon.
    std::function<void()> onLoginShellStarted = [] {};

private:
    bool isAskingForLoginShellInProjectFolder(const PtyOptions& options) const;

    std::unique_ptr<Shell> inner;
    ProjectSession session;
};
} // namespace term::phone
