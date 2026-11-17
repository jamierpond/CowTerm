#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// The signed token Apple's push service takes in place of a certificate: a
// JSON Web Token with header {"alg":"ES256","kid":<key id>} and claims
// {"iss":<team id>,"iat":<seconds since 1970>}, signed with the .p8 key.
namespace term::push
{
struct ApnsSigningKey
{
    // The .p8 file's text: a PKCS#8 PEM of a P-256 private key.
    std::string privateKeyPem;
    std::string keyId;
    std::string teamId;
};

struct ApnsProviderToken
{
    std::string token;

    // Empty when the token was made; otherwise why not, never the key itself.
    std::string error;
};

ApnsProviderToken createApnsProviderToken(const ApnsSigningKey& key,
                                          std::int64_t issuedAtSecondsSinceEpoch);

// RFC 4648 §5 without padding, as JSON Web Tokens use it.
std::string encodeBase64Url(std::string_view bytes);
} // namespace term::push
