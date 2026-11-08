#include "ParserFuzzInput.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::string_view {reinterpret_cast<const char*>(data), size};
    term::fuzz::runParserFuzzInput(bytes);
    return 0;
}
