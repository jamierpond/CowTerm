#include "ApnsProviderToken.h"

#include <Miro/Reflect.h>
#include <eacp/Core/Utils/Base64.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>

namespace term::push
{
namespace
{
constexpr auto signatureAlgorithm = "ES256";
constexpr auto sha256Bytes = std::size_t {32};
constexpr auto p256CoordinateBytes = std::size_t {32};
constexpr auto randomGeneratorLabel = std::string_view {"cowterm push relay"};
constexpr auto isSha224 = 0;

struct RandomGenerator
{
    RandomGenerator()
    {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&generator);
        const auto* label = reinterpret_cast<const unsigned char*>(randomGeneratorLabel.data());
        isSeeded = mbedtls_ctr_drbg_seed(&generator,
                                         mbedtls_entropy_func,
                                         &entropy,
                                         label,
                                         randomGeneratorLabel.size())
                   == 0;
    }

    ~RandomGenerator()
    {
        mbedtls_ctr_drbg_free(&generator);
        mbedtls_entropy_free(&entropy);
    }

    RandomGenerator(const RandomGenerator&) = delete;
    RandomGenerator& operator=(const RandomGenerator&) = delete;

    mbedtls_entropy_context entropy {};
    mbedtls_ctr_drbg_context generator {};
    bool isSeeded = false;
};

struct EcdsaKeyParts
{
    EcdsaKeyParts()
    {
        mbedtls_pk_init(&key);
        mbedtls_ecp_group_init(&group);
        mbedtls_mpi_init(&secret);
        mbedtls_ecp_point_init(&publicPoint);
        mbedtls_mpi_init(&r);
        mbedtls_mpi_init(&s);
    }

    ~EcdsaKeyParts()
    {
        mbedtls_mpi_free(&s);
        mbedtls_mpi_free(&r);
        mbedtls_ecp_point_free(&publicPoint);
        mbedtls_mpi_free(&secret);
        mbedtls_ecp_group_free(&group);
        mbedtls_pk_free(&key);
    }

    EcdsaKeyParts(const EcdsaKeyParts&) = delete;
    EcdsaKeyParts& operator=(const EcdsaKeyParts&) = delete;

    mbedtls_pk_context key {};
    mbedtls_ecp_group group {};
    mbedtls_mpi secret {};
    mbedtls_ecp_point publicPoint {};
    mbedtls_mpi r {};
    mbedtls_mpi s {};
};

std::array<unsigned char, sha256Bytes> hashWithSha256(std::string_view text)
{
    auto digest = std::array<unsigned char, sha256Bytes> {};
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    mbedtls_sha256(bytes, text.size(), digest.data(), isSha224);
    return digest;
}

std::string writeCoordinate(const mbedtls_mpi& value)
{
    auto bytes = std::array<unsigned char, p256CoordinateBytes> {};

    if (mbedtls_mpi_write_binary(&value, bytes.data(), bytes.size()) != 0)
        return {};

    const auto* characters = reinterpret_cast<const char*>(bytes.data());
    return {characters, bytes.size()};
}

std::string findProblemReadingKey(EcdsaKeyParts& parts,
                                  const std::string& privateKeyPem,
                                  RandomGenerator& random)
{
    const auto* pem = reinterpret_cast<const unsigned char*>(privateKeyPem.c_str());
    const auto pemLengthWithTerminator = privateKeyPem.size() + 1;

    if (mbedtls_pk_parse_key(&parts.key,
                             pem,
                             pemLengthWithTerminator,
                             nullptr,
                             0,
                             mbedtls_ctr_drbg_random,
                             &random.generator)
        != 0)
        return "the private key is not a PEM key mbedTLS can read";

    if (mbedtls_pk_get_type(&parts.key) != MBEDTLS_PK_ECKEY)
        return "the private key is not an elliptic-curve key";

    if (mbedtls_ecp_export(
            mbedtls_pk_ec(parts.key), &parts.group, &parts.secret, &parts.publicPoint)
        != 0)
        return "the private key's curve parameters could not be read";

    if (parts.group.id != MBEDTLS_ECP_DP_SECP256R1)
        return "the private key is not on the P-256 curve";

    return {};
}

std::string signWithEs256(EcdsaKeyParts& parts, std::string_view signingInput, RandomGenerator& random)
{
    const auto digest = hashWithSha256(signingInput);

    if (mbedtls_ecdsa_sign_det_ext(&parts.group,
                                   &parts.r,
                                   &parts.s,
                                   &parts.secret,
                                   digest.data(),
                                   digest.size(),
                                   MBEDTLS_MD_SHA256,
                                   mbedtls_ctr_drbg_random,
                                   &random.generator)
        != 0)
        return {};

    return writeCoordinate(parts.r) + writeCoordinate(parts.s);
}
} // namespace

std::string encodeBase64Url(std::string_view bytes)
{
    auto text = eacp::Base64::encode(bytes);
    std::ranges::replace(text, '+', '-');
    std::ranges::replace(text, '/', '_');
    std::erase(text, '=');
    return text;
}

namespace
{
struct TokenHeader
{
    std::string alg;
    std::string kid;

    MIRO_REFLECT(alg, kid)
};

struct TokenClaims
{
    std::string iss;
    std::int64_t iat = 0;

    MIRO_REFLECT(iss, iat)
};

} // namespace

ApnsProviderToken createApnsProviderToken(const ApnsSigningKey& key,
                                          std::int64_t issuedAtSecondsSinceEpoch)
{
    if (key.keyId.empty() || key.teamId.empty())
        return {{}, "the key id or the team id is missing"};

    auto random = RandomGenerator {};

    if (!random.isSeeded)
        return {{}, "no randomness to sign with"};

    auto parts = EcdsaKeyParts {};

    if (auto problem = findProblemReadingKey(parts, key.privateKeyPem, random); !problem.empty())
        return {{}, problem};

    const auto header = Miro::toJSONString(TokenHeader {signatureAlgorithm, key.keyId});
    const auto claims = Miro::toJSONString(TokenClaims {key.teamId, issuedAtSecondsSinceEpoch});
    const auto signingInput = encodeBase64Url(header) + "." + encodeBase64Url(claims);
    const auto signature = signWithEs256(parts, signingInput, random);

    if (signature.empty())
        return {{}, "signing failed"};

    return {signingInput + "." + encodeBase64Url(signature), {}};
}
} // namespace term::push
