#include "NotifyUserProtocol.h"
#include "Protocol.h"

namespace term
{
namespace
{
constexpr auto requestVerb = std::string_view {"notify-user"};
constexpr auto answeredVerb = std::string_view {"answered"};
constexpr auto dismissedVerb = std::string_view {"dismissed"};
constexpr auto typedResponseAllowed = std::string_view {"1"};
constexpr auto typedResponseRefused = std::string_view {"0"};

// Every field comes from argv, which cannot hold a NUL, so NUL separates them.
constexpr auto fieldSeparator = '\0';

constexpr auto paneIdField = std::size_t {0};
constexpr auto titleField = std::size_t {1};
constexpr auto allowsTypedResponseField = std::size_t {2};
constexpr auto contentField = std::size_t {3};
constexpr auto firstOptionField = std::size_t {4};

std::vector<std::string> splitFields(const std::string& payload)
{
    auto fields = std::vector<std::string> {};
    auto start = std::size_t {0};

    while (true)
    {
        const auto end = payload.find(fieldSeparator, start);

        if (end == std::string::npos)
        {
            fields.push_back(payload.substr(start));
            return fields;
        }

        fields.push_back(payload.substr(start, end - start));
        start = end + 1;
    }
}
} // namespace

std::string encodeNotifyUserRequest(const NotifyUserRequest& request)
{
    auto payload = request.paneId;
    payload += fieldSeparator;
    payload += request.title;
    payload += fieldSeparator;
    payload += request.allowsTypedResponse ? typedResponseAllowed
                                           : typedResponseRefused;
    payload += fieldSeparator;
    payload += request.content;

    for (const auto& option: request.options)
    {
        payload += fieldSeparator;
        payload += option;
    }

    // An empty payload would be dropped by proto::make, so the verb line always
    // ends in a newline here.
    return std::string {requestVerb} + '\n' + payload;
}

std::optional<NotifyUserRequest> decodeNotifyUserRequest(const std::string& message)
{
    const auto parsed = proto::parse(message);

    if (parsed.verb != requestVerb)
        return std::nullopt;

    const auto fields = splitFields(parsed.payload);

    if (fields.size() < firstOptionField)
        return std::nullopt;

    auto request = NotifyUserRequest {};
    request.paneId = fields[paneIdField];
    request.title = fields[titleField];
    request.allowsTypedResponse = fields[allowsTypedResponseField] == typedResponseAllowed;
    request.content = fields[contentField];

    for (auto index = firstOptionField; index < fields.size(); ++index)
        request.options.push_back(fields[index]);

    return request;
}

std::string encodeNotifyUserReply(const NotifyUserReply& reply)
{
    if (!reply.answer)
        return std::string {dismissedVerb};

    return std::string {answeredVerb} + '\n' + *reply.answer;
}

std::optional<NotifyUserReply> decodeNotifyUserReply(const std::string& message)
{
    const auto parsed = proto::parse(message);

    if (parsed.verb == dismissedVerb)
        return NotifyUserReply {};

    if (parsed.verb == answeredVerb)
        return NotifyUserReply {parsed.payload};

    return std::nullopt;
}
} // namespace term
