#include "Multiplexer/PaletteItem.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

namespace
{
constexpr std::int64_t nowMs = 1'790'000'000'000LL;
constexpr std::int64_t minuteMs = 60LL * 1000;

PaletteItem makeItem(PaletteItem::Kind kind, const std::string& label, const std::string& path)
{
    auto item = PaletteItem {};
    item.kind = kind;
    item.key = path;
    item.label = label;
    item.detail = path;
    return item;
}
} // namespace

auto tSessionAge = test("PaletteItem/aSessionRowSaysHowLongAgoItWasLastUsedAsThePhoneDoes") = []
{
    constexpr auto thirtySecondsMs = 30LL * 1000;
    constexpr auto fourMinutes = 4;
    constexpr auto thirtyHours = 30 * 60;
    auto session = makeItem(PaletteItem::Kind::Session, "grader", "/p/grader");

    session.lastUsed = nowMs - thirtySecondsMs;
    check(describePaletteItemAge(session, nowMs) == "just now");

    session.lastUsed = nowMs - fourMinutes * minuteMs;
    check(describePaletteItemAge(session, nowMs) == "4 min ago");

    session.lastUsed = nowMs - thirtyHours * minuteMs;
    check(describePaletteItemAge(session, nowMs) == "yesterday");
};

auto tNeverUsedHasNoAge = test("PaletteItem/aSessionNeverUsedShowsNoAge") = []
{
    const auto session = makeItem(PaletteItem::Kind::Session, "grader", "/p/grader");
    check(describePaletteItemAge(session, nowMs).empty());
};

auto tProjectHasNoAge = test("PaletteItem/aProjectRowShowsNoAgeEvenWhenOpenedBefore") = []
{
    auto project = makeItem(PaletteItem::Kind::Project, "graphics-analyzer", "/p/graphics-analyzer");
    project.lastUsed = nowMs - minuteMs;
    check(describePaletteItemAge(project, nowMs).empty());
};
