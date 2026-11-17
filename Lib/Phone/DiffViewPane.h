#pragma once

#include "Shell/Shell.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// dv (DiffView, a git diff viewer), the one program the phone starts: in a
// temporary pane of its own, in the folder of the pane on screen. The pane is
// killed when dv quits or the phone leaves it, and a record of it stays on
// disk until then, so a launch after the app was killed kills it.
namespace term::phone
{
inline constexpr auto diffViewCommand = "dv";

struct DiffViewPane
{
    std::string host;
    std::string paneId;
    std::string workingDirectory;
};

// dv for the pane on screen, in shownPaneFolder (its folder from the daemon's
// info), under a new pane id made from randomNumber. Empty while the folder is
// not known.
std::optional<DiffViewPane> makeDiffViewPane(const std::string& host,
                                             const std::string& shownPaneFolder,
                                             std::uint64_t randomNumber);

// "phone-dv-" then sixteen hex digits.
std::string makeDiffViewPaneId(std::uint64_t randomNumber);
bool isDiffViewPaneId(const std::string& paneId);

// Every pane id but the phone's own dv panes, which no list or dot shows.
std::vector<std::string> removeDiffViewPaneIds(std::vector<std::string> paneIds);

// "dv is not installed on macprorack."
std::string describeDiffViewNotInstalled(const std::string& hostLabel);

// What the phone says when dv ended having left text on the screen, which it
// does only when it could not start. Empty for a blank screen: dv quit.
std::string describeDiffViewFailure(const std::string& textLeftOnScreen, const std::string& folder);

// A dv pane the phone started and has not killed yet.
struct TemporaryPane
{
    std::string host;
    std::string paneId;

    bool operator==(const TemporaryPane&) const = default;
};

// The temporary panes on disk. Adding is false when the file could not be
// written, and then dv must not start.
bool addTemporaryPane(const std::filesystem::path& path, const TemporaryPane& pane);
void removeTemporaryPane(const std::filesystem::path& path, const TemporaryPane& pane);

// Empty when there is no file, or it does not parse.
std::vector<TemporaryPane> loadTemporaryPanes(const std::filesystem::path& path);

// The dv pane's shell: it starts dv in the pane's folder and nothing else.
// start() with any other command or folder starts nothing and returns false.
class DiffViewShell final : public Shell
{
public:
    DiffViewShell(std::unique_ptr<Shell> innerToUse, DiffViewPane paneToUse);

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

private:
    bool isAskingForDiffViewInItsFolder(const PtyOptions& options) const;

    std::unique_ptr<Shell> inner;
    DiffViewPane pane;
};
} // namespace term::phone
