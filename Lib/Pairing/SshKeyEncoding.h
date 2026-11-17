#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace term::pairing
{
inline constexpr auto ecdsaP256KeyTypeName = std::string_view {"ecdsa-sha2-nistp256"};

// The ssh wire form of an ECDSA P-256 public key (RFC 5656): the key type,
// the curve name and the point, each length-prefixed. uncompressedPoint is
// the 65 bytes 0x04 || X || Y, which SecKeyCopyExternalRepresentation gives.
std::string makeEcdsaP256PublicKeyBlob(std::string_view uncompressedPoint);

// "ecdsa-sha2-nistp256 AAAA... comment", one authorized_keys line.
std::string makeAuthorizedKeysLine(std::string_view publicKeyBlob, std::string_view comment);

// The key type a wire-form key names first ("ssh-ed25519"); empty when the
// blob is too short to hold one.
std::string readKeyTypeName(std::string_view keyBlob);

// An X9.62 DER signature (SEQUENCE of INTEGER r, INTEGER s) as the ssh
// ECDSA signature blob libssh2's sign callback returns: mpint r, mpint s.
// Empty when der is not that shape.
std::optional<std::string> convertDerSignatureToSshBlob(std::string_view der);
} // namespace term::pairing
