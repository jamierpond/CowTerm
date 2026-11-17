#include "Pairing/HostPairing.h"
#include "Pairing/SshKeyEncoding.h"

#include <eacp/Core/Utils/Base64.h>
#include <eacp/Core/Utils/Files.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>

using namespace nano;
using namespace term;
using namespace term::pairing;

namespace
{
// A throwaway P-256 key made with `openssl ecparam -name prime256v1 -genkey`;
// the line and fingerprint are what `ssh-keygen -y` and `ssh-keygen -l` print.
constexpr auto examplePointHex = "045838371b424a8d8118333ff12dbf5d89a4fee3b4e06f63f962ceb8807fb6d833"
                                 "80c8c7e278fd738698e73ac730f270cdf4183d58a22caada75256bfaa6b1bf89";
constexpr auto exampleKeyBase64 = "AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBFg4NxtCSo2BGDM/8S2/XYmk/uO04G9j+"
                                  "WLOuIB/ttgzgMjH4nj9c4aY5zrHMPJwzfQYPViiLKradSVr+qaxv4k=";
constexpr auto exampleFingerprint = "SHA256:VNykvWrDm9HTkgEOT8pkXP91hN+aiviEy4eGd+bwIzA";
constexpr auto hexDigitsPerByte = 2;
constexpr auto hexBase = 16;

std::string decodeHex(std::string_view hex)
{
    auto bytes = std::string {};

    for (auto index = std::size_t {0}; index < hex.size(); index += hexDigitsPerByte)
    {
        const auto pair = std::string {hex.substr(index, hexDigitsPerByte)};
        const auto value = std::stoi(pair, nullptr, hexBase);
        bytes.push_back(static_cast<char>(value));
    }

    return bytes;
}

std::string makeExampleKeyBlob()
{
    return *eacp::Base64::decode(exampleKeyBase64);
}

std::string makeOtherKeyBlob()
{
    auto point = decodeHex(examplePointHex);
    point.back() = static_cast<char>(point.back() ^ 1);
    return makeEcdsaP256PublicKeyBlob(point);
}

SshDestination makeDestination(const std::string& user, const std::string& host, int port = defaultSshPort)
{
    return {user, host, port};
}

struct PairingFolder
{
    PairingFolder()
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~PairingFolder() { std::filesystem::remove_all(path); }

    std::filesystem::path path = std::filesystem::temp_directory_path() / "CowTermPairingTests";
};
} // namespace

auto publicKeyBlobMatchesSshKeygen = test("Pairing/publicKeyBlobMatchesSshKeygen") = []
{
    const auto blob = makeEcdsaP256PublicKeyBlob(decodeHex(examplePointHex));

    check(eacp::Base64::encode(blob) == exampleKeyBase64);
};

auto authorizedKeysLineNamesTypeKeyAndComment = test("Pairing/authorizedKeysLineNamesTypeKeyAndComment") = []
{
    const auto line = makeAuthorizedKeysLine(makeExampleKeyBlob(), "cowterm-iphone");

    check(line == std::string {"ecdsa-sha2-nistp256 "} + exampleKeyBase64 + " cowterm-iphone");
};

auto keyTypeNameIsReadFromTheBlob = test("Pairing/keyTypeNameIsReadFromTheBlob") = []
{
    check(readKeyTypeName(makeExampleKeyBlob()) == ecdsaP256KeyTypeName);
    check(readKeyTypeName("\x00\x00").empty());
    check(readKeyTypeName(std::string {"\x00\x00\x00\x09ssh", 7}).empty());
};

auto derSignatureBecomesTwoMpints = test("Pairing/derSignatureBecomesTwoMpints") = []
{
    const auto der = decodeHex("300a020300800102037f0102");
    const auto blob = convertDerSignatureToSshBlob(der);

    check(blob.has_value());
    check(*blob == decodeHex("00000003008001" "000000037f0102"));
};

auto malformedDerSignatureIsRefused = test("Pairing/malformedDerSignatureIsRefused") = []
{
    check(!convertDerSignatureToSshBlob({}).has_value());
    check(!convertDerSignatureToSshBlob(decodeHex("300a020300800102037f01")).has_value());
    check(!convertDerSignatureToSshBlob(decodeHex("310a020300800102037f0102")).has_value());
    check(!convertDerSignatureToSshBlob(decodeHex("300a040300800102037f0102")).has_value());
};

auto fingerprintMatchesSshKeygen = test("Pairing/fingerprintMatchesSshKeygen") = []
{
    check(formatHostKeyFingerprint(makeExampleKeyBlob()) == exampleFingerprint);
};

auto hostKeyCheckCommandNamesTheKeyFile = test("Pairing/hostKeyCheckCommandNamesTheKeyFile") = []
{
    check(makeHostKeyCheckCommand(makeExampleKeyBlob()) == "ssh-keygen -lf /etc/ssh/ssh_host_ecdsa_key.pub");
};

auto typedHostForgivesSpacesAndSsh = test("Pairing/typedHostForgivesSpacesAndSsh") = []
{
    constexpr auto otherPort = 2222;
    const auto typed = parseTypedHost("  ssh jamie@studio.local:2222 ");

    check(typed.has_value());
    check(typed->user == "jamie");
    check(typed->host == "studio.local");
    check(typed->port == otherPort);
};

auto typedHostNeedsUserAtHost = test("Pairing/typedHostNeedsUserAtHost") = []
{
    check(!parseTypedHost("studio").has_value());
    check(!parseTypedHost("jamie@").has_value());
    check(!parseTypedHost("jamie @studio").has_value());
    check(!parseTypedHost("").has_value());
};

auto knownHostsLineBracketsAnOtherPort = test("Pairing/knownHostsLineBracketsAnOtherPort") = []
{
    constexpr auto otherPort = 2222;
    const auto onDefaultPort = makeKnownHostsLine(makeDestination("jamie", "studio"), makeExampleKeyBlob());
    const auto onOtherPort = makeKnownHostsLine(makeDestination("jamie", "studio", otherPort), makeExampleKeyBlob());

    check(onDefaultPort == std::string {"studio ecdsa-sha2-nistp256 "} + exampleKeyBase64);
    check(onOtherPort == std::string {"[studio]:2222 ecdsa-sha2-nistp256 "} + exampleKeyBase64);
};

auto hostKeyTrustIsKnownUnknownOrChanged = test("Pairing/hostKeyTrustIsKnownUnknownOrChanged") = []
{
    const auto studio = makeDestination("jamie", "studio");
    const auto knownHosts = "# comment\nother,studio ecdsa-sha2-nistp256 " + std::string {exampleKeyBase64} + "\n";

    check(checkHostKey(knownHosts, studio, makeExampleKeyBlob()) == HostKeyTrust::Known);
    check(checkHostKey(knownHosts, studio, makeOtherKeyBlob()) == HostKeyTrust::Changed);
    check(checkHostKey(knownHosts, makeDestination("jamie", "laptop"), makeExampleKeyBlob())
          == HostKeyTrust::Unknown);
    check(checkHostKey({}, studio, makeExampleKeyBlob()) == HostKeyTrust::Unknown);
};

auto unknownHostKeyWaitsForConfirmation = test("Pairing/unknownHostKeyWaitsForConfirmation") = []
{
    const auto folder = PairingFolder {};
    const auto pairedHosts = PairedHosts {folder.path};
    auto pairing = HostPairing {pairedHosts};

    check(pairing.submitHost("jamie@studio"));
    check(pairing.getStep() == HostPairingStep::FetchingHostKey);

    pairing.receiveHostKey(makeExampleKeyBlob());

    check(pairing.getStep() == HostPairingStep::ConfirmingFingerprint);
    check(pairing.getFingerprint() == exampleFingerprint);
    check(pairedHosts.readHostLines().empty());
    check(pairedHosts.readKnownHosts().empty());

    pairing.confirm();

    check(pairing.getStep() == HostPairingStep::Paired);
    check(pairedHosts.readHostLines() == std::vector<std::string> {"jamie@studio"});
    check(checkHostKey(pairedHosts.readKnownHosts(), makeDestination("jamie", "studio"), makeExampleKeyBlob())
          == HostKeyTrust::Known);
};

auto startingAgainWritesNothing = test("Pairing/startingAgainWritesNothing") = []
{
    const auto folder = PairingFolder {};
    const auto pairedHosts = PairedHosts {folder.path};
    auto pairing = HostPairing {pairedHosts};

    pairing.submitHost("jamie@studio");
    pairing.receiveHostKey(makeExampleKeyBlob());
    pairing.startAgain();
    pairing.confirm();

    check(pairing.getStep() == HostPairingStep::EnteringHost);
    check(pairedHosts.readHostLines().empty());
    check(pairedHosts.readKnownHosts().empty());
};

auto knownHostKeyPairsWithoutAsking = test("Pairing/knownHostKeyPairsWithoutAsking") = []
{
    const auto folder = PairingFolder {};
    const auto pairedHosts = PairedHosts {folder.path};
    const auto studio = makeDestination("jamie", "studio");
    pairedHosts.trustHostKey(studio, makeExampleKeyBlob());
    auto pairing = HostPairing {pairedHosts};

    pairing.submitHost("root@studio");
    pairing.receiveHostKey(makeExampleKeyBlob());

    check(pairing.getStep() == HostPairingStep::Paired);
    check(pairedHosts.readHostLines() == std::vector<std::string> {"root@studio"});
};

auto changedHostKeyIsRefused = test("Pairing/changedHostKeyIsRefused") = []
{
    const auto folder = PairingFolder {};
    const auto pairedHosts = PairedHosts {folder.path};
    pairedHosts.trustHostKey(makeDestination("jamie", "studio"), makeExampleKeyBlob());
    auto pairing = HostPairing {pairedHosts};

    pairing.submitHost("jamie@studio");
    pairing.receiveHostKey(makeOtherKeyBlob());
    pairing.confirm();

    check(pairing.getStep() == HostPairingStep::HostKeyChanged);
    check(pairedHosts.readHostLines().empty());
};

auto badHostTextExplainsTheForm = test("Pairing/badHostTextExplainsTheForm") = []
{
    const auto folder = PairingFolder {};
    const auto pairedHosts = PairedHosts {folder.path};
    auto pairing = HostPairing {pairedHosts};

    check(!pairing.submitHost("studio"));
    check(pairing.getStep() == HostPairingStep::EnteringHost);
    check(!pairing.getMessage().empty());
};

auto failureIsShownAndHostIsNotAdded = test("Pairing/failureIsShownAndHostIsNotAdded") = []
{
    const auto folder = PairingFolder {};
    const auto pairedHosts = PairedHosts {folder.path};
    auto pairing = HostPairing {pairedHosts};

    pairing.submitHost("jamie@studio");
    pairing.receiveFailure("ssh: Could not resolve hostname studio");

    check(pairing.getStep() == HostPairingStep::Failed);
    check(pairing.getMessage() == "ssh: Could not resolve hostname studio");
    check(pairedHosts.readHostLines().empty());
};

auto addingAHostTwiceKeepsOneLine = test("Pairing/addingAHostTwiceKeepsOneLine") = []
{
    const auto folder = PairingFolder {};
    const auto pairedHosts = PairedHosts {folder.path};
    const auto studio = makeDestination("jamie", "studio");

    check(pairedHosts.addHost(studio));
    check(!pairedHosts.addHost(studio));
    check(pairedHosts.readHostLines().size() == 1);
};
