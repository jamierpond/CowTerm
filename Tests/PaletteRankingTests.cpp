#include "Multiplexer/PaletteRanking.h"

#include <NanoTest/NanoTest.h>

#include <map>

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

std::vector<PaletteItem> makeItems()
{
    return {makeItem(PaletteItem::Kind::Session, "grader", "/p/grader"),
            makeItem(PaletteItem::Kind::Session, "Grazer", "/p/Grazer"),
            makeItem(PaletteItem::Kind::Project, "graphics-analyzer", "/p/graphics-analyzer"),
            makeItem(PaletteItem::Kind::Project, "Grazer worktree", "/p/Grazer-wt")};
}

std::vector<std::string> getKeys(const std::vector<PaletteItem>& items)
{
    auto keys = std::vector<std::string> {};

    for (auto& item: items)
        keys.push_back(item.key);

    return keys;
}

PaletteUsage makeUsage(std::map<std::string, double> usageByKey)
{
    auto usage = [usageByKey = std::move(usageByKey)](const std::string& key)
    {
        auto found = usageByKey.find(key);

        if (found == usageByKey.end())
            return 0.0;

        return found->second;
    };

    return {usage, {}};
}
} // namespace

auto tLevelsThenUsage = test("PaletteRanking/name start first, scattered letters last, usage within a level") = []
{
    auto ranked = rankPaletteItems(makeItems(), "grazer", makeUsage({{"/p/Grazer-wt", 3.0}}));

    check(getKeys(ranked) == std::vector<std::string> {"/p/Grazer-wt", "/p/Grazer", "/p/graphics-analyzer"});
};

auto tOpenBeforeClosed = test("PaletteRanking/an open session beats a project on an equal match and usage") = []
{
    auto items = std::vector<PaletteItem> {makeItem(PaletteItem::Kind::Project, "web", "/a/web"),
                                           makeItem(PaletteItem::Kind::Session, "web", "/b/web")};

    check(rankPaletteItems(items, "web", {}).front().key == "/b/web");
};

auto tLearnedPickFirst = test("PaletteRanking/a session picked twice for the text comes first") = []
{
    auto learned = frecencysearch::addPickTo({"gra", "/p/grader"}, nowMs - 2 * minuteMs);
    learned = frecencysearch::addPickTo(learned, nowMs - minuteMs);

    auto usage = makeUsage({{"/p/Grazer", 50.0}});
    usage.learnedPicks = frecencysearch::findLearnedPicksForQuery({learned}, "gra", nowMs);

    check(rankPaletteItems(makeItems(), "gra", usage).front().key == "/p/grader");
};

namespace
{
std::vector<PaletteItem> makeItemsOnSeveralMachines()
{
    auto local = makeItem(PaletteItem::Kind::Project, "Miro", "/p/Miro");
    auto onMacprorack =
        makeItem(PaletteItem::Kind::Project, "Miro", "macprorack:/home/j/projects/Miro");
    onMacprorack.host = "macprorack";
    auto onTamby = makeItem(PaletteItem::Kind::Project, "Miro", "tamby:/Users/j/projects/Miro");
    onTamby.host = "jamie@tamby";
    return {local, onMacprorack, onTamby};
}
} // namespace

auto tHostThenName = test("PaletteRanking/host then name finds the remote project") = []
{
    const auto ranked = rankPaletteItems(makeItemsOnSeveralMachines(), "macprorackmiro", makeUsage({}));

    check(!ranked.empty());
    check(ranked.front().key == "macprorack:/home/j/projects/Miro");
};

auto tNameThenHost = test("PaletteRanking/name then host finds it too") = []
{
    const auto ranked = rankPaletteItems(makeItemsOnSeveralMachines(), "mirotamby", makeUsage({}));

    check(!ranked.empty());
    check(ranked.front().key == "tamby:/Users/j/projects/Miro");
};

auto tPlainNameKeepsEveryMachine = test("PaletteRanking/a plain name still lists it on every machine") = []
{
    const auto ranked =
        rankPaletteItems(makeItemsOnSeveralMachines(), "miro", makeUsage({{"/p/Miro", 1.0}}));

    check(ranked.size() == 3);
    check(ranked.front().key == "/p/Miro");
};

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
