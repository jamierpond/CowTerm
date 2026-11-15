#pragma once

#include <optional>
#include <string>
#include <vector>

namespace term
{
// What `ct notify-user` asks the running app to show.
struct NotifyUserRequest
{
    std::string paneId;
    std::string title;
    std::string content;
    std::vector<std::string> options;
    bool allowsTypedResponse = false;
};

// The user's answer: an option's text or the typed text, or nothing when the
// prompt was dismissed.
struct NotifyUserReply
{
    std::optional<std::string> answer;
};

std::string encodeNotifyUserRequest(const NotifyUserRequest& request);
std::optional<NotifyUserRequest> decodeNotifyUserRequest(const std::string& message);

std::string encodeNotifyUserReply(const NotifyUserReply& reply);
std::optional<NotifyUserReply> decodeNotifyUserReply(const std::string& message);
} // namespace term
