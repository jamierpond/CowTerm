#include "Multiplexer/DataDirectories.h"

#include <chrono>
#include <filesystem>
#include <thread>

// clang's coverage runtime, linked in by -fprofile-instr-generate.
extern "C" void __llvm_profile_set_filename(const char* filenamePattern);
extern "C" int __llvm_profile_write_file();
extern "C" void __llvm_profile_reset_counters();

namespace term
{
namespace
{
constexpr auto countsWriteInterval = std::chrono::minutes {1};

// %m: every run of one executable merges into one file, so each program keeps one.
constexpr auto countsFileName = "%m.profraw";

void writeCountsEveryInterval()
{
    while (true)
    {
        std::this_thread::sleep_for(countsWriteInterval);

        // The counts are added to the file, so clearing them keeps the next write from adding them twice.
        if (__llvm_profile_write_file() == 0)
            __llvm_profile_reset_counters();
    }
}

struct LineCoverageWriter
{
    LineCoverageWriter()
    {
        const auto directory = getLineCoverageDirectory();
        auto error = std::error_code {};
        std::filesystem::create_directories(directory, error);

        __llvm_profile_set_filename((directory / countsFileName).string().c_str());
        std::thread {writeCountsEveryInterval}.detach();
    }
};

const auto lineCoverageWriter = LineCoverageWriter {};
} // namespace
} // namespace term
