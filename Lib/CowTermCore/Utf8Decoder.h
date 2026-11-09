#pragma once

#include <array>
#include <cstdint>

namespace term
{
// Turns UTF-8 bytes into codepoints one byte at a time, so a codepoint split
// across two reads still decodes. A malformed byte becomes U+FFFD.
class Utf8Decoder
{
public:
    static constexpr auto replacementCharacter = char32_t {0xFFFD};

    template <typename EmitCodepoint>
    void decode(std::uint8_t byte, EmitCodepoint&& emitCodepoint)
    {
        if (remainingContinuationBytes > 0)
            continueSequence(byte, emitCodepoint);
        else
            startSequence(byte, emitCodepoint);
    }

    void reset() { remainingContinuationBytes = 0; }

private:
    struct LeadByteShape
    {
        std::uint8_t mask;
        std::uint8_t value;
        std::uint8_t payloadMask;
        int continuationBytes;
    };

    static constexpr std::array<LeadByteShape, 3> leadByteShapes = {{
        {0xe0, 0xc0, 0x1f, 1},
        {0xf0, 0xe0, 0x0f, 2},
        {0xf8, 0xf0, 0x07, 3},
    }};

    static constexpr std::uint8_t firstNonAsciiByte = 0x80;
    static constexpr std::uint8_t continuationMask = 0xc0;
    static constexpr std::uint8_t continuationValue = 0x80;
    static constexpr std::uint8_t continuationPayloadMask = 0x3f;
    static constexpr int continuationPayloadBits = 6;

    static bool isContinuationByte(std::uint8_t byte)
    {
        return (byte & continuationMask) == continuationValue;
    }

    template <typename EmitCodepoint>
    void continueSequence(std::uint8_t byte, EmitCodepoint& emitCodepoint)
    {
        if (!isContinuationByte(byte))
        {
            remainingContinuationBytes = 0;
            emitCodepoint(replacementCharacter);
            startSequence(byte, emitCodepoint);
            return;
        }

        codepoint = (codepoint << continuationPayloadBits) | (byte & continuationPayloadMask);

        if (--remainingContinuationBytes == 0)
            emitCodepoint(codepoint);
    }

    template <typename EmitCodepoint>
    void startSequence(std::uint8_t byte, EmitCodepoint& emitCodepoint)
    {
        if (byte < firstNonAsciiByte)
        {
            emitCodepoint((char32_t) byte);
            return;
        }

        for (const auto& shape: leadByteShapes)
        {
            if ((byte & shape.mask) == shape.value)
            {
                codepoint = byte & shape.payloadMask;
                remainingContinuationBytes = shape.continuationBytes;
                return;
            }
        }

        emitCodepoint(replacementCharacter);
    }

    char32_t codepoint = 0;
    int remainingContinuationBytes = 0;
};
} // namespace term
