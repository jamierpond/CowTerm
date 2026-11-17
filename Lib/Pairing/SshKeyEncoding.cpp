#include "SshKeyEncoding.h"

#include <eacp/Core/Utils/Base64.h>

#include <cstdint>

namespace term::pairing
{
namespace
{
constexpr auto curveName = std::string_view {"nistp256"};
constexpr auto lengthPrefixBytes = std::size_t {4};
constexpr auto bitsPerByte = 8;
constexpr auto derSequenceTag = 0x30;
constexpr auto derIntegerTag = 0x02;
constexpr auto derLongLengthFlag = 0x80;

void appendLengthPrefixed(std::string& out, std::string_view bytes)
{
    const auto length = static_cast<std::uint32_t>(bytes.size());

    for (auto shift = static_cast<int>(lengthPrefixBytes - 1); shift >= 0; --shift)
    {
        const auto byte = static_cast<char>((length >> (shift * bitsPerByte)) & 0xff);
        out.push_back(byte);
    }

    out.append(bytes);
}

std::uint8_t readByte(std::string_view bytes, std::size_t at)
{
    return static_cast<std::uint8_t>(bytes[at]);
}

// One DER element's tag and contents at `at`; short-form lengths only, which
// is all an ECDSA P-256 signature uses.
struct DerElement
{
    std::uint8_t tag = 0;
    std::string_view contents;
    std::size_t end = 0;
};

std::optional<DerElement> readDerElement(std::string_view der, std::size_t at)
{
    if (at + 2 > der.size())
        return std::nullopt;

    const auto length = std::size_t {readByte(der, at + 1)};

    if ((length & derLongLengthFlag) != 0 || at + 2 + length > der.size())
        return std::nullopt;

    return DerElement {readByte(der, at), der.substr(at + 2, length), at + 2 + length};
}

std::optional<DerElement> readDerElementTagged(std::string_view der, std::size_t at, std::uint8_t tag)
{
    auto element = readDerElement(der, at);

    if (!element || element->tag != tag)
        return std::nullopt;

    return element;
}

// The contents of the one element der holds, when it has the tag and nothing follows it.
std::optional<std::string_view> readWholeDerElement(std::string_view der, std::uint8_t tag)
{
    const auto element = readDerElementTagged(der, 0, tag);

    if (!element || element->end != der.size())
        return std::nullopt;

    return element->contents;
}
} // namespace

std::string makeEcdsaP256PublicKeyBlob(std::string_view uncompressedPoint)
{
    auto blob = std::string {};
    appendLengthPrefixed(blob, ecdsaP256KeyTypeName);
    appendLengthPrefixed(blob, curveName);
    appendLengthPrefixed(blob, uncompressedPoint);
    return blob;
}

std::string makeAuthorizedKeysLine(std::string_view publicKeyBlob, std::string_view comment)
{
    return readKeyTypeName(publicKeyBlob) + " " + eacp::Base64::encode(publicKeyBlob) + " " + std::string {comment};
}

std::string readKeyTypeName(std::string_view keyBlob)
{
    if (keyBlob.size() < lengthPrefixBytes)
        return {};

    auto length = std::size_t {0};

    for (auto index = std::size_t {0}; index < lengthPrefixBytes; ++index)
        length = (length << bitsPerByte) | readByte(keyBlob, index);

    if (lengthPrefixBytes + length > keyBlob.size())
        return {};

    return std::string {keyBlob.substr(lengthPrefixBytes, length)};
}

// A DER INTEGER's contents are already an mpint's: big-endian two's complement
// with no needless leading byte, so each is copied with a length in front.
std::optional<std::string> convertDerSignatureToSshBlob(std::string_view der)
{
    const auto sequence = readWholeDerElement(der, derSequenceTag);

    if (!sequence)
        return std::nullopt;

    const auto r = readDerElementTagged(*sequence, 0, derIntegerTag);

    if (!r)
        return std::nullopt;

    const auto s = readDerElementTagged(*sequence, r->end, derIntegerTag);

    if (!s || s->end != sequence->size())
        return std::nullopt;

    auto blob = std::string {};
    appendLengthPrefixed(blob, r->contents);
    appendLengthPrefixed(blob, s->contents);
    return blob;
}
} // namespace term::pairing
