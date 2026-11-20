#include "GitHubPageOpener.h"

#include "Command.h"
#include "Multiplexer/UsageLog.h"

#include <eacp/Core/Process/Process.h>

namespace term::github
{
namespace
{
std::string getPageKindName(PageKind kind)
{
    if (kind == PageKind::pullRequest)
        return "pull-request";

    return "repository";
}

std::string getFirstLine(const std::string& text)
{
    return text.substr(0, text.find_first_of("\r\n"));
}

// ssh's own complaint ("Could not resolve hostname") says more than an
// empty answer.
std::string describeFailure(const RepositoryFacts& facts,
                            const PageChoice& choice,
                            const eacp::Processes::ProcessResult& result)
{
    const auto errorLine = getFirstLine(result.errorOutput);

    if (!result.launched)
        return "Could not start the command that reads the repository.";

    if (isQueryUnanswered(facts) && !errorLine.empty())
        return choice.failureReason + " " + errorLine;

    return choice.failureReason;
}

eacp::Processes::ProcessOptions makeQueryOptions(const PaneFolder& folder,
                                                 PageKind kind)
{
    const auto command = makeRepositoryQueryCommand(folder, kind);

    auto options = eacp::Processes::ProcessOptions {};
    options.executable = command.front();
    options.arguments.assign(command.begin() + 1, command.end());
    return options;
}
} // namespace

void openPageForFolder(const PaneFolder& folder,
                       PageKind kind,
                       std::function<void(const std::string& reason)> onFailure)
{
    logUsage("github.query",
             {{"page", getPageKindName(kind)}, {"host", folder.host}});

    eacp::Processes::runAsync(makeQueryOptions(folder, kind))
        .then(
            [kind, host = folder.host, onFailure = std::move(onFailure)](
                const eacp::Processes::ProcessResult& result)
            {
                const auto facts = parseRepositoryQueryOutput(result.output);
                const auto choice = choosePage(facts, kind);

                if (!choice.url.empty())
                {
                    openInBrowser(choice.url);
                    return;
                }

                const auto reason = describeFailure(facts, choice, result);
                logUsage("github.failed",
                         {{"page", getPageKindName(kind)},
                          {"host", host},
                          {"reason", reason}});
                onFailure(reason);
            });
}
} // namespace term::github
