#include "SessionLayout.h"

#include "Config.h"
#include "Protocol.h"

namespace term
{
std::string normalizedDir(const std::string& dir)
{
    auto path = expandHome(dir);

    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    return path;
}

std::string makeLayoutMessage(const SavedSession& session)
{
    return proto::make("layout", {proto::encodeArgument(session.name)}, Miro::toJSONString(session));
}
} // namespace term
