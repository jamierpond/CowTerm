#include <CowTermTerminal/OutputParserThread.h>

#include <NanoTest/NanoTest.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

using namespace nano;
using namespace term;

namespace
{
constexpr auto largestChunk = std::size_t {64 * 1024};

struct RecordingParser
{
    std::mutex lock;
    std::string parsedBytes;
    std::size_t largestPiece = 0;
    std::atomic<int> batches {0};

    OutputParserThread thread {[this](std::string_view bytes)
                               {
                                   auto guard = std::scoped_lock {lock};
                                   parsedBytes += bytes;
                                   largestPiece = std::max(largestPiece, bytes.size());
                               },
                               [this] { ++batches; }};
};
} // namespace

auto tBytesFromAnotherThreadArriveInOrder = test("OutputThread/bytesFromAnotherThreadParseInOrder") = []
{
    constexpr auto pieceCount = 1000;
    auto recording = RecordingParser {};
    auto expected = std::string {};

    for (auto piece = 0; piece < pieceCount; ++piece)
        expected += std::to_string(piece) + ",";

    auto writer = std::thread(
        [&]
        {
            for (auto piece = 0; piece < pieceCount; ++piece)
                recording.thread.append(std::to_string(piece) + ",");
        });

    writer.join();
    recording.thread.waitUntilIdle();

    check(recording.parsedBytes == expected);
    check(recording.batches > 0);
};

auto tLargeOutputIsParsedInChunks = test("OutputThread/aFloodIsParsedInChunks") = []
{
    constexpr auto floodSize = 5 * largestChunk + 17;
    auto recording = RecordingParser {};

    recording.thread.append(std::string(floodSize, 'x'));
    recording.thread.waitUntilIdle();

    check(recording.parsedBytes.size() == floodSize);
    check(recording.largestPiece <= largestChunk);
};

auto tStopsWithOutputPending = test("OutputThread/destroyingWithPendingOutputReturns") = []
{
    constexpr auto floodSize = 50 * largestChunk;
    auto recording = std::make_unique<RecordingParser>();

    recording->thread.append(std::string(floodSize, 'x'));
    recording.reset();

    check(true, "the destructor joined without hanging");
};
