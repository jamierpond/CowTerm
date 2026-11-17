#include "NoPaneYetShell.h"

namespace cowterm::mobile
{
namespace
{
class NoPaneYetShell final : public term::Shell
{
public:
    bool start(const term::PtyOptions&, std::function<void(std::string)>, std::function<void()>) override
    {
        return true;
    }

    void write(std::string_view) override {}
    void resize(const term::PtySize&) override {}

    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }

    void terminate() override {}
    void detach() override {}
};
} // namespace

term::TerminalConfig makeNoPaneYetConfig()
{
    auto config = term::TerminalConfig {};
    config.makeShell = [](const std::string&) { return std::make_unique<NoPaneYetShell>(); };
    return config;
}
} // namespace cowterm::mobile
