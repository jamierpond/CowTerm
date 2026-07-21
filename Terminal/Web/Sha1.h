#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace term::web
{
// SHA-1 + base64, vendored for the one place the web stack needs them: the
// RFC 6455 Sec-WebSocket-Accept handshake digest. Not a general-purpose
// crypto surface — SHA-1 is fine here because the handshake value is an
// integrity nonce, not a security boundary.
inline std::array<std::uint8_t, 20> sha1(std::string_view data)
{
    auto h = std::array<std::uint32_t, 5> {
        0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};

    auto message = std::string {data};
    const auto bitLength = (std::uint64_t) message.size() * 8;

    message.push_back((char) 0x80);

    while (message.size() % 64 != 56)
        message.push_back('\0');

    for (auto shift = 56; shift >= 0; shift -= 8)
        message.push_back((char) ((bitLength >> shift) & 0xff));

    const auto rotl = [](std::uint32_t value, int bits)
    { return (value << bits) | (value >> (32 - bits)); };

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64)
    {
        std::uint32_t w[80];

        for (auto i = 0; i < 16; ++i)
        {
            w[i] = ((std::uint32_t) (std::uint8_t) message[chunk + i * 4] << 24)
                   | ((std::uint32_t) (std::uint8_t) message[chunk + i * 4 + 1] << 16)
                   | ((std::uint32_t) (std::uint8_t) message[chunk + i * 4 + 2] << 8)
                   | (std::uint32_t) (std::uint8_t) message[chunk + i * 4 + 3];
        }

        for (auto i = 16; i < 80; ++i)
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (auto i = 0; i < 80; ++i)
        {
            std::uint32_t f, k;

            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5a827999u;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcu;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }

            const auto temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    auto digest = std::array<std::uint8_t, 20> {};

    for (auto i = 0; i < 5; ++i)
    {
        digest[(std::size_t) i * 4] = (std::uint8_t) (h[(std::size_t) i] >> 24);
        digest[(std::size_t) i * 4 + 1] = (std::uint8_t) (h[(std::size_t) i] >> 16);
        digest[(std::size_t) i * 4 + 2] = (std::uint8_t) (h[(std::size_t) i] >> 8);
        digest[(std::size_t) i * 4 + 3] = (std::uint8_t) h[(std::size_t) i];
    }

    return digest;
}

inline std::string base64(const std::uint8_t* data, std::size_t length)
{
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    auto out = std::string {};
    out.reserve((length + 2) / 3 * 4);

    for (std::size_t i = 0; i < length; i += 3)
    {
        const auto b0 = data[i];
        const auto b1 = i + 1 < length ? data[i + 1] : (std::uint8_t) 0;
        const auto b2 = i + 2 < length ? data[i + 2] : (std::uint8_t) 0;

        out.push_back(table[b0 >> 2]);
        out.push_back(table[((b0 & 0x03) << 4) | (b1 >> 4)]);
        out.push_back(i + 1 < length ? table[((b1 & 0x0f) << 2) | (b2 >> 6)] : '=');
        out.push_back(i + 2 < length ? table[b2 & 0x3f] : '=');
    }

    return out;
}

inline std::string websocketAcceptKey(const std::string& clientKey)
{
    const auto digest =
        sha1(clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return base64(digest.data(), digest.size());
}
} // namespace term::web
