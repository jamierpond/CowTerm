#pragma once

#include <cstddef>

// ct bridge's byte pipe to ssh: stdin and stdout, unbuffered and binary.
namespace term::bridge
{
// Binary mode on Windows (no CRLF translation); a vanished reader is an
// error from write rather than a SIGPIPE on POSIX.
void prepareStandardStreams();

// Bytes read, 0 at end of input, negative on error. Blocks.
long readStandardInput(char* buffer, std::size_t size);

// Bytes written, negative on error.
long writeStandardOutput(const char* bytes, std::size_t size);
} // namespace term::bridge
