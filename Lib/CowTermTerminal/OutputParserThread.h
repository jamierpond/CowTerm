#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace term
{
// Parses a pane's shell output on a thread of its own, as Ghostty's termio
// thread does, so a flood of output (cat of a large file) never stalls the UI
// thread. Bytes may arrive on any thread; parse runs on the worker in pieces
// of at most a chunk, so the UI can take the terminal lock between them, and
// parsed runs after each batch.
class OutputParserThread
{
public:
    OutputParserThread(std::function<void(std::string_view)> parseToUse,
                       std::function<void()> parsedToUse);
    ~OutputParserThread();

    void append(std::string_view bytes);

    // Blocks until every byte appended so far has been parsed.
    void waitUntilIdle();

private:
    void run();
    void parseInChunks(std::string_view bytes);

    std::function<void(std::string_view)> parse;
    std::function<void()> parsed;

    std::mutex lock;
    std::condition_variable wake;
    std::condition_variable idle;
    std::string pending;
    bool parsing = false;
    std::atomic<bool> stopping {false};
    std::thread worker;
};
} // namespace term
