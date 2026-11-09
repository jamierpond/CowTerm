#include "OutputParserThread.h"

#include <algorithm>

namespace term
{
namespace
{
constexpr auto largestChunk = std::size_t {64 * 1024};
}

OutputParserThread::OutputParserThread(std::function<void(std::string_view)> parseToUse,
                                       std::function<void()> parsedToUse)
    : parse(std::move(parseToUse))
    , parsed(std::move(parsedToUse))
    , worker([this] { run(); })
{
}

OutputParserThread::~OutputParserThread()
{
    {
        auto guard = std::scoped_lock {lock};
        stopping = true;
    }

    wake.notify_one();
    worker.join();
}

void OutputParserThread::append(std::string_view bytes)
{
    {
        auto guard = std::scoped_lock {lock};
        pending += bytes;
    }

    wake.notify_one();
}

void OutputParserThread::waitUntilIdle()
{
    auto guard = std::unique_lock {lock};
    idle.wait(guard, [this] { return pending.empty() && !parsing; });
}

void OutputParserThread::run()
{
    auto batch = std::string {};
    auto guard = std::unique_lock {lock};

    while (true)
    {
        wake.wait(guard, [this] { return stopping || !pending.empty(); });

        if (stopping)
            return;

        batch.clear();
        batch.swap(pending);
        parsing = true;
        guard.unlock();

        parseInChunks(batch);
        parsed();

        guard.lock();
        parsing = false;

        if (pending.empty())
            idle.notify_all();
    }
}

void OutputParserThread::parseInChunks(std::string_view bytes)
{
    while (!bytes.empty() && !stopping)
    {
        const auto chunk = std::min(bytes.size(), largestChunk);
        parse(bytes.substr(0, chunk));
        bytes.remove_prefix(chunk);
        std::this_thread::yield();
    }
}
} // namespace term
