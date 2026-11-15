#include "PaletteRanking.h"

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
