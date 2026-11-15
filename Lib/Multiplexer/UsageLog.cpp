#include "UsageLog.h"
#include "DataDirectories.h"

#include <SessionLog/SessionLog.h>

#include <eacp/Core/Utils/Logging.h>

#include <chrono>

namespace term
{
namespace
{
constexpr auto sessionLogsToKeep = std::size_t {50};

std::int64_t getNowEpochMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace

void startUsageLog()
{
    const auto moves = moveOldAppData();
    const auto logsDirectory = getLogsDirectory().string();
    eacp::setLogFile(sessionlog::getSessionLogPath(logsDirectory, getNowEpochMs()));
    sessionlog::deleteOldSessionLogs(logsDirectory, sessionLogsToKeep);

    for (const auto& move: moves)
        logUsage("app.migrate",
                 {{"from", move.from.string()}, {"to", move.to.string()}, {"error", move.error}});
}

void logUsage(std::string_view event, std::initializer_list<sessionlog::Field> fields)
{
    eacp::LOG(sessionlog::formatEventLine(event, fields));
}
} // namespace term
