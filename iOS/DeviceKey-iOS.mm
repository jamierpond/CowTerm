#include "DeviceKey.h"

#include "Pairing/SshKeyEncoding.h"

#include <eacp/Core/Utils/Logging.h>

#include <cstring>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace cowterm::mobile
{
namespace
{
constexpr auto keyTag = "audio.pond.cowterm.mobile.device-key";
constexpr auto p256KeyBits = 256;

NSData* makeKeyTagData()
{
    return [NSData dataWithBytes:keyTag length:std::strlen(keyTag)];
}

SecKeyRef findStoredKey()
{
    NSDictionary* query = @{
        (id) kSecClass: (id) kSecClassKey,
        (id) kSecAttrApplicationTag: makeKeyTagData(),
        (id) kSecAttrKeyType: (id) kSecAttrKeyTypeECSECPrimeRandom,
        (id) kSecReturnRef: @YES,
    };
    CFTypeRef found = nullptr;

    if (SecItemCopyMatching((__bridge CFDictionaryRef) query, &found) != errSecSuccess)
        return nullptr;

    return (SecKeyRef) found;
}

SecKeyRef createKey(bool inSecureEnclave)
{
    CFErrorRef error = nullptr;
    const auto accessFlags = inSecureEnclave ? kSecAccessControlPrivateKeyUsage : SecAccessControlCreateFlags {0};
    SecAccessControlRef access = SecAccessControlCreateWithFlags(
        kCFAllocatorDefault, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly, accessFlags, &error);

    if (access == nullptr)
        return nullptr;

    NSMutableDictionary* attributes = [@{
        (id) kSecAttrKeyType: (id) kSecAttrKeyTypeECSECPrimeRandom,
        (id) kSecAttrKeySizeInBits: @(p256KeyBits),
        (id) kSecPrivateKeyAttrs: @{
            (id) kSecAttrIsPermanent: @YES,
            (id) kSecAttrApplicationTag: makeKeyTagData(),
            (id) kSecAttrAccessControl: (__bridge id) access,
        },
    } mutableCopy];

    if (inSecureEnclave)
        attributes[(id) kSecAttrTokenID] = (id) kSecAttrTokenIDSecureEnclave;

    SecKeyRef key = SecKeyCreateRandomKey((__bridge CFDictionaryRef) attributes, &error);
    CFRelease(access);

    if (key == nullptr)
    {
        NSError* reason = CFBridgingRelease(error);
        eacp::LOG("device key: could not make one", inSecureEnclave ? " in the Secure Enclave: " : ": ",
            reason.localizedDescription.UTF8String);
    }

    return key;
}

bool isSecureEnclaveKey(SecKeyRef key)
{
    NSDictionary* attributes = CFBridgingRelease(SecKeyCopyAttributes(key));
    return [attributes[(id) kSecAttrTokenID] isEqual:(id) kSecAttrTokenIDSecureEnclave];
}

std::optional<std::string> readPublicKeyBlob(SecKeyRef privateKey)
{
    SecKeyRef publicKey = SecKeyCopyPublicKey(privateKey);

    if (publicKey == nullptr)
        return std::nullopt;

    NSData* point = CFBridgingRelease(SecKeyCopyExternalRepresentation(publicKey, nullptr));
    CFRelease(publicKey);

    if (point == nil)
        return std::nullopt;

    const auto pointBytes = std::string_view {static_cast<const char*>(point.bytes), point.length};
    return term::pairing::makeEcdsaP256PublicKeyBlob(pointBytes);
}

// Signs on whichever thread libssh2 asks from; SecKeyCreateSignature is thread-safe.
std::optional<std::string> signWithKey(SecKeyRef key, std::string_view data)
{
    NSData* message = [NSData dataWithBytes:data.data() length:data.size()];
    NSData* der = CFBridgingRelease(SecKeyCreateSignature(
        key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256, (__bridge CFDataRef) message, nullptr));

    if (der == nil)
        return std::nullopt;

    const auto derBytes = std::string_view {static_cast<const char*>(der.bytes), der.length};
    return term::pairing::convertDerSignatureToSshBlob(derBytes);
}

// The key lives as long as the app: the sign function holds a reference to it.
SecKeyRef findOrCreateKey()
{
    if (auto stored = findStoredKey())
        return stored;

    if (auto inEnclave = createKey(true))
        return inEnclave;

    return createKey(false);
}
} // namespace

std::optional<DeviceKey> loadOrCreateDeviceKey()
{
    SecKeyRef key = findOrCreateKey();

    if (key == nullptr)
        return std::nullopt;

    auto publicKeyBlob = readPublicKeyBlob(key);

    if (!publicKeyBlob)
    {
        CFRelease(key);
        return std::nullopt;
    }

    auto deviceKey = DeviceKey {};
    deviceKey.isInSecureEnclave = isSecureEnclaveKey(key);
    deviceKey.signingKey.publicKeyBlob = std::move(*publicKeyBlob);
    deviceKey.signingKey.sign = [key](std::string_view data) { return signWithKey(key, data); };
    return deviceKey;
}
} // namespace cowterm::mobile
