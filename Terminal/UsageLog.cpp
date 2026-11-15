#include "UsageLog.h"

#include <SessionLog/SessionLog.h>

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Core/Utils/StdPath.h>

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
    const auto logsDirectory =
        (eacp::toStdPath(eacp::FilePath::appDataDirectory()) / "CowTerm" / "logs").string();
    eacp::setLogFile(sessionlog::getSessionLogPath(logsDirectory, getNowEpochMs()));
    sessionlog::deleteOldSessionLogs(logsDirectory, sessionLogsToKeep);
}

void logUsage(std::string_view event, std::initializer_list<sessionlog::Field> fields)
{
    eacp::LOG(sessionlog::formatEventLine(event, fields));
}
} // namespace term
