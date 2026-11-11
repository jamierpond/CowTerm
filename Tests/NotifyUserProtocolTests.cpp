#include "NotifyUserProtocol.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto tRequestRoundTrips = test("NotifyUserProtocol/requestRoundTrips") = []
{
    auto request = NotifyUserRequest {};
    request.paneId = "0123456789abcdef";
    request.title = "tamber-web · fix-auth";
    request.content = "first line\nsecond line with spaces";
    request.options = {"ship it", "hold", ""};
    request.allowsTypedResponse = true;

    const auto decoded = decodeNotifyUserRequest(encodeNotifyUserRequest(request));

    check(decoded.has_value());
    check(decoded->paneId == request.paneId);
    check(decoded->title == request.title);
    check(decoded->content == request.content, "multi-line content survives");
    check(decoded->options == request.options, "options keep order and empties");
    check(decoded->allowsTypedResponse);
};

auto tRequestWithNothingOptional = test("NotifyUserProtocol/requestWithNothingOptional") = []
{
    auto request = NotifyUserRequest {};
    request.paneId = "abc";
    request.content = "done";

    const auto decoded = decodeNotifyUserRequest(encodeNotifyUserRequest(request));

    check(decoded.has_value());
    check(decoded->title.empty());
    check(decoded->options.empty());
    check(!decoded->allowsTypedResponse);
};

auto tAnsweredReplyRoundTrips = test("NotifyUserProtocol/answeredReplyRoundTrips") = []
{
    const auto decoded =
        decodeNotifyUserReply(encodeNotifyUserReply({std::string {"that one"}}));

    check(decoded.has_value());
    check(decoded->answer == std::optional<std::string> {"that one"});
};

auto tEmptyAnswerIsNotDismissal = test("NotifyUserProtocol/emptyAnswerIsNotDismissal") = []
{
    const auto decoded = decodeNotifyUserReply(encodeNotifyUserReply({std::string {}}));

    check(decoded.has_value());
    check(decoded->answer.has_value(), "an empty typed answer is still an answer");
};

auto tDismissedReplyRoundTrips = test("NotifyUserProtocol/dismissedReplyRoundTrips") = []
{
    const auto decoded = decodeNotifyUserReply(encodeNotifyUserReply({}));

    check(decoded.has_value());
    check(!decoded->answer.has_value());
};

auto tRejectsOtherMessages = test("NotifyUserProtocol/rejectsOtherMessages") = []
{
    check(!decodeNotifyUserRequest("spawn abc 80 24").has_value());
    check(!decodeNotifyUserRequest("notify-user").has_value(), "no fields at all");
    check(!decodeNotifyUserReply("output abc\nhello").has_value());
};
