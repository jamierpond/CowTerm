#include "OverviewSamples.h"

#include <chrono>

namespace cowterm::mobile
{
using term::overview::OverviewRow;

namespace
{
constexpr auto oneMinute = std::int64_t {60'000};
constexpr auto laptop = "jamiepond@tamby";
constexpr auto rack = "jamiepond@macprorack";
constexpr auto desk = "jamiepond@big-cow";
constexpr auto sampleHostCount = 3;

struct SampleSession
{
    const char* host;
    const char* name;
    bool isClaude;
    bool isWaiting;
    const char* stateLine;
    int minutesSinceTyping;
};

constexpr SampleSession sampleSessions[] = {
    {desk, "CowTerm", true, true, "waiting: Run the full test suite now?", 2},
    {laptop, "tamber-web", true, true, "waiting: Push jp/shader-refactor to origin?", 9},
    {desk, "Grazer", true, false, "working, 4 min", 1},
    {rack, "miro", false, false, "finished 12 min ago", 12},
    {desk, "eacp", true, false, "finished 20 min ago", 20},
    {laptop, "basic-cpp-server", false, false, "idle", 35},
    {desk, "dotfiles", false, false, "idle", 48},
    {rack, "Rudder", true, false, "finished 1 h ago", 61},
    {desk, "DiffView", false, false, "idle", 75},
    {laptop, "iac", false, false, "idle", 90},
    {desk, "codehealth", true, false, "finished 2 h ago", 130},
    {rack, "emberstore", false, false, "idle", 180},
    {desk, "ScreenCap", false, false, "idle", 240},
    {laptop, "text-buffer", false, false, "idle", 300},
};

std::int64_t getMillisecondsSinceEpochNow()
{
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count();
}

OverviewRow makeRow(const SampleSession& sample, std::int64_t now)
{
    auto row = OverviewRow {};
    row.host = sample.host;
    row.sessionName = sample.name;
    row.paneIds = {std::string {sample.name} + "-1"};
    row.isClaude = sample.isClaude;
    row.isWaiting = sample.isWaiting;
    row.stateLine = sample.stateLine;
    row.lastUsedMilliseconds = now - sample.minutesSinceTyping * oneMinute;
    return row;
}
} // namespace

OverviewInputs makeSampleOverviewInputs()
{
    auto inputs = OverviewInputs {};
    inputs.nowMilliseconds = getMillisecondsSinceEpochNow();
    inputs.hostCount = sampleHostCount + 1;
    inputs.unreachableHosts = {{"jamie@thinkpad", "ssh: connect timed out after 5 s"}};

    for (const auto& sample: sampleSessions)
        inputs.sessions.push_back(makeRow(sample, inputs.nowMilliseconds));

    return inputs;
}
} // namespace cowterm::mobile
