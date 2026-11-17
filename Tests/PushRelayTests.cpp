#include <PushRelay/ApnsDelivery.h>
#include <PushRelay/ApnsProviderToken.h>
#include <PushRelay/PushRelay.h>

#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>
#include <eacp/Core/Utils/Base64.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <filesystem>
#include <vector>

using namespace nano;
using namespace term::push;

namespace
{
// A throwaway P-256 key made for these tests with
// `openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve`,
// the same PKCS#8 form as a .p8 file from Apple.
constexpr auto testPrivateKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgEYvsUFOfwJdm8AL/
kg2QRRmo+eFFitgx5FGWN1bqjlahRANCAASaNQgpz0SrSvQpDBl7ZezuDtgklQxk
vjDeuR0dipUiHJeQt11jBx/YjT7ExoVzfdIdUDptgpTTwwVFmusfZErL
-----END PRIVATE KEY-----
)";

// The same key's public point, uncompressed (0x04 || x || y), as hex.
constexpr auto testPublicPointHex = "049a350829cf44ab4af4290c197b65ecee0ed824950c64be30deb91d1d8a952"
                                    "21c9790b75d63071fd88d3ec4c685737dd21d503a6d8294d3c305459aeb1f6"
                                    "44acb";

constexpr auto testKeyId = "ABC123DEFG";
constexpr auto testTeamId = "DEF123GHIJ";
constexpr auto issuedAtSeconds = std::int64_t {1'763'380'800};
constexpr auto fortyOneMinutesSeconds = std::int64_t {41 * 60};
constexpr auto signatureBytes = std::size_t {64};
constexpr auto coordinateBytes = std::size_t {32};
constexpr auto base64QuantumCharacters = std::size_t {4};
constexpr auto sha256Bytes = std::size_t {32};
constexpr auto isSha224 = 0;
constexpr auto deviceToken = "a3f1c2d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90";
constexpr auto otherDeviceToken = "b3f1c2d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90";
constexpr auto topic = "audio.pond.cowterm.mobile";
constexpr auto okStatus = 200;
constexpr auto badRequestStatus = 400;
constexpr auto forbiddenStatus = 403;
constexpr auto goneStatus = 410;
constexpr auto tooManyRequestsStatus = 429;
constexpr auto unavailableStatus = 503;
constexpr auto maximumAttempts = std::size_t {3};

const auto signingKey = ApnsSigningKey {testPrivateKeyPem, testKeyId, testTeamId};

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

std::vector<std::string> splitAtDots(const std::string& token)
{
    auto parts = std::vector<std::string> {};
    auto start = std::size_t {0};

    for (auto dot = token.find('.'); dot != std::string::npos; dot = token.find('.', start))
    {
        parts.push_back(token.substr(start, dot - start));
        start = dot + 1;
    }

    parts.push_back(token.substr(start));
    return parts;
}

std::string decodeBase64Url(std::string text)
{
    std::ranges::replace(text, '-', '+');
    std::ranges::replace(text, '_', '/');

    while (text.size() % base64QuantumCharacters != 0)
        text += '=';

    return eacp::Base64::decode(text).value_or("");
}

std::vector<unsigned char> readHexBytes(const std::string& hex)
{
    constexpr auto hexDigitsPerByte = std::size_t {2};
    constexpr auto hexadecimal = 16;
    auto bytes = std::vector<unsigned char> {};

    for (auto index = std::size_t {0}; index + 1 < hex.size(); index += hexDigitsPerByte)
    {
        const auto value = std::stoi(hex.substr(index, hexDigitsPerByte), nullptr, hexadecimal);
        bytes.push_back(static_cast<unsigned char>(value));
    }

    return bytes;
}

bool isSignatureMadeByTestKey(const std::string& signingInput, const std::string& signature)
{
    auto group = mbedtls_ecp_group {};
    auto publicPoint = mbedtls_ecp_point {};
    auto r = mbedtls_mpi {};
    auto s = mbedtls_mpi {};
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&publicPoint);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    auto digest = std::array<unsigned char, sha256Bytes> {};
    const auto* input = reinterpret_cast<const unsigned char*>(signingInput.data());
    mbedtls_sha256(input, signingInput.size(), digest.data(), isSha224);

    const auto* signatureData = reinterpret_cast<const unsigned char*>(signature.data());
    mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&r, signatureData, coordinateBytes);
    mbedtls_mpi_read_binary(&s, signatureData + coordinateBytes, coordinateBytes);

    const auto pointBytes = readHexBytes(testPublicPointHex);

    const auto isPointRead =
        mbedtls_ecp_point_read_binary(&group, &publicPoint, pointBytes.data(), pointBytes.size()) == 0;
    const auto isVerified =
        isPointRead
        && mbedtls_ecdsa_verify(&group, digest.data(), digest.size(), &publicPoint, &r, &s) == 0;

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&publicPoint);
    mbedtls_ecp_group_free(&group);
    return isVerified;
}

eacp::HTTP::Response makeAppleAnswer(int statusCode, const std::string& reason = {})
{
    auto response = eacp::HTTP::Response {};
    response.statusCode = statusCode;

    if (!reason.empty())
        response.content = R"({"reason":")" + reason + R"("})";

    return response;
}

std::string makeRegistration(const std::string& token, const std::string& environment = "sandbox")
{
    return R"({"name":"pond","token":")" + token + R"(","environment":")" + environment + R"("})";
}

constexpr auto finishedNotice =
    R"({"host":"tamby","session":"Grazer","paneId":"a1b2","event":"finished","text":"Done."})";

// A relay whose Apple answers from a script, one answer per request, and
// whose clock and waits are recorded instead of passing.
struct RelayUnderTest
{
    explicit RelayUnderTest(std::vector<eacp::HTTP::Response> answersToGive)
        : answers(std::move(answersToGive))
    {
        std::filesystem::remove(deviceFile);
    }

    ~RelayUnderTest() { std::filesystem::remove(deviceFile); }

    PushRelay makeRelay()
    {
        auto settings = PushRelaySettings {signingKey, topic, "default", deviceFile};
        auto services = PushRelayServices {};
        services.sendToApple = [this](const eacp::HTTP::Request& request)
        {
            requests.push_back(request);
            const auto answerIndex = std::min(requests.size(), answers.size()) - 1;
            return answers[answerIndex];
        };
        services.getSecondsSinceEpoch = [this] { return now; };
        services.wait = [this](std::chrono::milliseconds delay) { waits.push_back(delay); };
        services.log = [this](const std::string& line) { log.push_back(line); };
        return PushRelay {settings, services};
    }

    std::filesystem::path deviceFile =
        std::filesystem::temp_directory_path() / "cowterm-push-relay-test-devices.json";
    std::vector<eacp::HTTP::Response> answers;
    std::vector<eacp::HTTP::Request> requests;
    std::vector<std::chrono::milliseconds> waits;
    std::vector<std::string> log;
    std::int64_t now = issuedAtSeconds;
};
} // namespace

auto tTokenHasAppleHeaderAndClaims = test("ApnsProviderToken/hasAppleHeaderAndClaims") = []
{
    const auto made = createApnsProviderToken(signingKey, issuedAtSeconds);
    check(made.error.empty(), made.error);

    const auto parts = splitAtDots(made.token);
    check(parts.size() == 3);

    if (parts.size() != 3)
        return;

    const auto header = Miro::createFromJSONString<TokenHeader>(decodeBase64Url(parts[0]));
    const auto claims = Miro::createFromJSONString<TokenClaims>(decodeBase64Url(parts[1]));
    check(header.alg == "ES256" && header.kid == testKeyId);
    check(claims.iss == testTeamId && claims.iat == issuedAtSeconds);
    check(made.token.find('=') == std::string::npos, "JSON Web Tokens carry no padding");
};

auto tTokenSignatureVerifiesWithThePublicKey = test("ApnsProviderToken/signatureVerifiesWithThePublicKey") = []
{
    const auto made = createApnsProviderToken(signingKey, issuedAtSeconds);
    const auto parts = splitAtDots(made.token);
    check(parts.size() == 3);

    if (parts.size() != 3)
        return;

    const auto signature = decodeBase64Url(parts[2]);
    check(signature.size() == signatureBytes, "ES256 is r and s, 32 bytes each, not DER");
    check(isSignatureMadeByTestKey(parts[0] + "." + parts[1], signature));
};

auto tTokenWithABadKeySaysWhyWithoutTheKey = test("ApnsProviderToken/withABadKeySaysWhyWithoutTheKey") = []
{
    const auto made = createApnsProviderToken({"not a key", testKeyId, testTeamId}, issuedAtSeconds);
    check(made.token.empty());
    check(!made.error.empty());
    check(made.error.find("not a key") == std::string::npos);
};

auto tTokenWithoutKeyIdIsRefused = test("ApnsProviderToken/withoutKeyIdIsRefused") = []
{
    check(!createApnsProviderToken({testPrivateKeyPem, "", testTeamId}, issuedAtSeconds).error.empty());
};

auto tBase64UrlUsesTheUrlAlphabet = test("ApnsProviderToken/base64UrlUsesTheUrlAlphabet") = []
{
    check(encodeBase64Url("\xfb\xff") == "-_8");
};

auto tRequestGoesToTheSandboxWithAppleHeaders = test("ApnsDelivery/requestGoesToTheSandboxWithAppleHeaders") = []
{
    const auto request =
        buildApnsRequest({deviceToken, ApnsEnvironment::sandbox, topic, "{}", "signed.token.here"});
    check(request.url == std::string {"https://api.sandbox.push.apple.com/3/device/"} + deviceToken,
          request.url);
    check(request.type == "POST");
    check(request.body == "{}");
    check(request.getHeader("authorization") == "bearer signed.token.here");
    check(request.getHeader("apns-topic") == topic);
    check(request.getHeader("apns-push-type") == "alert");
    check(request.getHeader("apns-priority") == "10");
};

auto tProductionGoesToProduction = test("ApnsDelivery/productionGoesToProduction") = []
{
    const auto request = buildApnsRequest({deviceToken, ApnsEnvironment::production, topic, "{}", "t"});
    check(request.url.starts_with("https://api.push.apple.com/3/device/"), request.url);
};

auto tAppleAnswersMeanWhatAppleDocuments = test("ApnsDelivery/appleAnswersMeanWhatAppleDocuments") = []
{
    struct Case
    {
        int statusCode;
        std::string reason;
        ApnsOutcome outcome;
    };

    const auto cases = std::vector<Case> {
        {okStatus, "", ApnsOutcome::delivered},
        {forbiddenStatus, "ExpiredProviderToken", ApnsOutcome::renewProviderTokenAndRetry},
        {forbiddenStatus, "InvalidProviderToken", ApnsOutcome::giveUp},
        {badRequestStatus, "BadDeviceToken", ApnsOutcome::forgetDevice},
        {badRequestStatus, "DeviceTokenNotForTopic", ApnsOutcome::forgetDevice},
        {badRequestStatus, "BadTopic", ApnsOutcome::giveUp},
        {goneStatus, "Unregistered", ApnsOutcome::forgetDevice},
        {tooManyRequestsStatus, "TooManyRequests", ApnsOutcome::retryLater},
        {tooManyRequestsStatus, "TooManyProviderTokenUpdates", ApnsOutcome::retryLater},
        {unavailableStatus, "ServiceUnavailable", ApnsOutcome::retryLater},
        {unavailableStatus, "SomethingNew", ApnsOutcome::retryLater},
        {badRequestStatus, "SomethingNew", ApnsOutcome::giveUp},
    };

    for (const auto& oneCase: cases)
    {
        const auto result = interpretApnsResponse(makeAppleAnswer(oneCase.statusCode, oneCase.reason));
        check(result.outcome == oneCase.outcome, oneCase.reason);
        check(result.reason == oneCase.reason, oneCase.reason);
        check(!result.explanation.empty(), oneCase.reason);
    }
};

auto tUnreachableAppleIsRetried = test("ApnsDelivery/unreachableAppleIsRetried") = []
{
    auto response = eacp::HTTP::Response {};
    response.error = "The Internet connection appears to be offline.";
    const auto result = interpretApnsResponse(response);
    check(result.outcome == ApnsOutcome::retryLater);
    check(result.explanation.find("offline") != std::string::npos);
};

auto tRegistrationIsCheckedAndKept = test("PushRelay/registrationIsCheckedAndKept") = []
{
    auto fixture = RelayUnderTest {{}};
    auto relay = fixture.makeRelay();
    check(relay.registerDevice(makeRegistration("not hex")).statusCode == badRequestStatus);
    check(relay.registerDevice(makeRegistration(deviceToken, "staging")).statusCode == badRequestStatus);
    check(relay.registerDevice(makeRegistration(deviceToken)).statusCode == okStatus);
    check(relay.registerDevice(makeRegistration(otherDeviceToken)).statusCode == okStatus);

    const auto devices = fixture.makeRelay().getDevices();
    check(devices.size() == 1, "a second registration of pond replaces the first");
    check(!devices.empty() && devices.front().token == otherDeviceToken);
};

auto tNoticeReachesARegisteredPhone = test("PushRelay/noticeReachesARegisteredPhone") = []
{
    auto fixture = RelayUnderTest {{makeAppleAnswer(okStatus)}};
    auto relay = fixture.makeRelay();
    relay.registerDevice(makeRegistration(deviceToken));
    const auto answer = relay.deliverNotice(finishedNotice);
    check(answer.statusCode == okStatus, answer.text);
    check(fixture.requests.size() == 1);
    check(!fixture.requests.empty()
          && fixture.requests.front().body.find("tamby/Grazer") != std::string::npos);
};

auto tNoticeWithoutPhonesSendsNothing = test("PushRelay/noticeWithoutPhonesSendsNothing") = []
{
    auto fixture = RelayUnderTest {{makeAppleAnswer(okStatus)}};
    auto relay = fixture.makeRelay();
    check(relay.deliverNotice(finishedNotice).statusCode == okStatus);
    check(fixture.requests.empty());
};

auto tExpiredProviderTokenIsRenewedOnce = test("PushRelay/expiredProviderTokenIsRenewedOnce") = []
{
    auto fixture = RelayUnderTest {
        {makeAppleAnswer(forbiddenStatus, "ExpiredProviderToken"), makeAppleAnswer(okStatus)}};
    auto relay = fixture.makeRelay();
    relay.registerDevice(makeRegistration(deviceToken));
    check(relay.deliverNotice(finishedNotice).statusCode == okStatus);
    check(fixture.requests.size() == 2);
    check(fixture.waits.empty());
};

auto tProviderTokenIsReusedThenRenewedWhenOld = test("PushRelay/providerTokenIsReusedThenRenewedWhenOld") = []
{
    auto fixture = RelayUnderTest {{makeAppleAnswer(okStatus)}};
    auto relay = fixture.makeRelay();
    relay.registerDevice(makeRegistration(deviceToken));
    relay.deliverNotice(finishedNotice);
    relay.deliverNotice(finishedNotice);
    fixture.now += fortyOneMinutesSeconds;
    relay.deliverNotice(finishedNotice);
    check(fixture.requests.size() == 3);

    if (fixture.requests.size() == 3)
    {
        const auto first = fixture.requests[0].getHeader("authorization");
        check(first == fixture.requests[1].getHeader("authorization"));
        check(first != fixture.requests[2].getHeader("authorization"));
    }
};

auto tBadDeviceTokenForgetsThePhone = test("PushRelay/badDeviceTokenForgetsThePhone") = []
{
    auto fixture = RelayUnderTest {{makeAppleAnswer(badRequestStatus, "BadDeviceToken")}};
    auto relay = fixture.makeRelay();
    relay.registerDevice(makeRegistration(deviceToken));
    check(relay.deliverNotice(finishedNotice).statusCode != okStatus);
    check(relay.getDevices().empty());
    check(fixture.makeRelay().getDevices().empty(), "forgotten on disk too");
};

auto tRateLimitedPushIsRetriedWithGrowingWaits = test("PushRelay/rateLimitedPushIsRetriedWithGrowingWaits") = []
{
    auto fixture = RelayUnderTest {{makeAppleAnswer(tooManyRequestsStatus, "TooManyRequests")}};
    auto relay = fixture.makeRelay();
    relay.registerDevice(makeRegistration(deviceToken));
    check(relay.deliverNotice(finishedNotice).statusCode != okStatus);
    check(fixture.requests.size() == maximumAttempts);
    check(fixture.waits.size() == maximumAttempts);
    check(fixture.waits.size() == maximumAttempts && fixture.waits[1] > fixture.waits[0]);
    check(relay.getDevices().size() == 1, "a rate limit keeps the phone");
};

auto tEveryAppleAnswerIsLogged = test("PushRelay/everyAppleAnswerIsLogged") = []
{
    auto fixture = RelayUnderTest {{makeAppleAnswer(goneStatus, "Unregistered")}};
    auto relay = fixture.makeRelay();
    relay.registerDevice(makeRegistration(deviceToken));
    relay.deliverNotice(finishedNotice);

    const auto hasAppleLine = std::ranges::any_of(
        fixture.log,
        [](const std::string& line)
        { return line.find("Apple answered 410 Unregistered") != std::string::npos; });
    check(hasAppleLine);
};

auto tMalformedNoticeIsRefused = test("PushRelay/malformedNoticeIsRefused") = []
{
    auto fixture = RelayUnderTest {{makeAppleAnswer(okStatus)}};
    auto relay = fixture.makeRelay();
    check(relay.deliverNotice("{}").statusCode == badRequestStatus);
};
