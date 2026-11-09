#include <CowTermTerminal/TerminalView.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Runs `cat` of a large coloured file in a real pane (local PTY, no daemon)
// and reports how long the pane takes to show its last line, and how late a
// task posted to the UI thread runs meanwhile: the wait a key press would see.
//
//   CowTermCatThroughputBench [LINES]
using namespace eacp;
using Clock = std::chrono::steady_clock;

namespace
{
constexpr auto defaultLineCount = 200000;
constexpr auto benchTimeout = Time::MS {300000};
constexpr auto pollSlice = Time::MS {5};
constexpr auto viewWidth = 1200.f;
constexpr auto viewHeight = 800.f;
constexpr auto endMarker = std::string_view {"END-OF-CAT"};
constexpr auto percentile99 = 0.99;
constexpr auto microsecondsPerMillisecond = 1000.0;

std::filesystem::path writeColouredFile(int lineCount)
{
    const auto path = std::filesystem::temp_directory_path() / "cowterm-cat-bench.txt";
    auto file = std::ofstream {path};

    for (auto line = 0; line < lineCount; ++line)
        file << "\033[3" << line % 8 << "m" << line
             << " the quick brown fox jumps over the lazy dog 0123456789 abcdefghij\033[0m\n";

    file << endMarker << "\n";
    return path;
}

struct UiLatency
{
    std::vector<double> samplesMs;
    Clock::time_point postedAt;
    bool stopped = false;

    void postPing()
    {
        postedAt = Clock::now();
        Threads::callAsync(
            [this]
            {
                const auto waited = std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - postedAt);
                samplesMs.push_back((double) waited.count() / microsecondsPerMillisecond);

                if (!stopped)
                    postPing();
            });
    }

    double findPercentile(double fraction)
    {
        if (samplesMs.empty())
            return 0;

        std::sort(samplesMs.begin(), samplesMs.end());
        const auto index = (std::size_t) (fraction * (double) (samplesMs.size() - 1));
        return samplesMs[index];
    }
};
} // namespace

int main(int argc, char** argv)
{
    const auto lineCount = argc > 1 ? std::stoi(argv[1]) : defaultLineCount;
    const auto path = writeColouredFile(lineCount);
    const auto bytes = std::filesystem::file_size(path);

    auto config = term::TerminalConfig {};
    const auto started = Clock::now();
    auto view = term::TerminalView {config, "/tmp", "", "cat " + path.string()};
    view.setBounds({0.f, 0.f, viewWidth, viewHeight});
    view.resized();

    auto latency = UiLatency {};
    latency.postPing();

    const auto finished = Threads::runEventLoopUntil(
        [&] { return view.debugScreenText().find(endMarker) != std::string::npos; },
        benchTimeout,
        pollSlice);

    latency.stopped = true;
    const auto seconds = std::chrono::duration<double>(Clock::now() - started).count();

    std::printf("cat %zu bytes (%d lines): %s in %.2f s, %.1f MB/s\n",
                (std::size_t) bytes,
                lineCount,
                finished ? "shown" : "TIMED OUT",
                seconds,
                (double) bytes / seconds / 1e6);
    std::printf("UI thread wait: p50 %.2f ms, p99 %.2f ms, max %.2f ms (%zu samples)\n",
                latency.findPercentile(0.5),
                latency.findPercentile(percentile99),
                latency.findPercentile(1.0),
                latency.samplesMs.size());

    std::filesystem::remove(path);
    return finished ? 0 : 1;
}
