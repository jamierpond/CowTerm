#include "EmbeddedSshLink.h"

#include "RemoteLocation.h"

#include <eacp/Core/Threads/Async.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <libssh2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace term
{
using namespace eacp;

// What the main thread and the ssh thread share: bytes waiting to go out, the
// request to stop, and a pipe that wakes the ssh thread out of poll().
struct EmbeddedSshConnection
{
    EmbeddedSshConnection()
    {
        auto ends = std::array<int, 2> {-1, -1};

        if (::pipe(ends.data()) == 0)
        {
            wakeReader = ends[0];
            wakeWriter = ends[1];
            ::fcntl(wakeReader, F_SETFL, O_NONBLOCK);
            ::fcntl(wakeWriter, F_SETFL, O_NONBLOCK);
        }
    }

    ~EmbeddedSshConnection()
    {
        ::close(wakeReader);
        ::close(wakeWriter);
    }

    void queueBytes(const std::string& bytes)
    {
        {
            const auto lock = std::lock_guard {mutex};
            outgoing += bytes;
        }

        wake();
    }

    std::string takeQueuedBytes()
    {
        const auto lock = std::lock_guard {mutex};
        return std::exchange(outgoing, {});
    }

    void stop()
    {
        stopping = true;
        wake();
    }

    bool isStopping() const { return stopping; }

    void drainWakes() const
    {
        auto discarded = std::array<char, 64> {};

        while (::read(wakeReader, discarded.data(), discarded.size()) > 0)
        {
        }
    }

    int wakeReader = -1;
    int wakeWriter = -1;

private:
    void wake() const
    {
        const auto byte = char {0};
        [[maybe_unused]] const auto written = ::write(wakeWriter, &byte, 1);
    }

    std::mutex mutex;
    std::string outgoing;
    std::atomic<bool> stopping = false;
};

namespace
{
constexpr auto setupTimeoutMilliseconds = 10000;
constexpr auto keepaliveIntervalSeconds = 5;
constexpr auto millisecondsPerSecond = 1000;
constexpr auto readBufferBytes = std::size_t {65536};
constexpr auto hostKeyVerificationFailed = "Host key verification failed.";
constexpr auto timedOut = "Operation timed out";

struct SessionEnding
{
    int exitCode = sshFailedExitCode;
    std::string errorOutput;
};

struct HostKeyType
{
    int sessionType;
    int knownHostType;
    std::string_view name;
};

constexpr auto hostKeyTypes = std::array {
    HostKeyType {LIBSSH2_HOSTKEY_TYPE_RSA, LIBSSH2_KNOWNHOST_KEY_SSHRSA, "ssh-rsa"},
    HostKeyType {LIBSSH2_HOSTKEY_TYPE_ECDSA_256, LIBSSH2_KNOWNHOST_KEY_ECDSA_256, "ecdsa-sha2-nistp256"},
    HostKeyType {LIBSSH2_HOSTKEY_TYPE_ECDSA_384, LIBSSH2_KNOWNHOST_KEY_ECDSA_384, "ecdsa-sha2-nistp384"},
    HostKeyType {LIBSSH2_HOSTKEY_TYPE_ECDSA_521, LIBSSH2_KNOWNHOST_KEY_ECDSA_521, "ecdsa-sha2-nistp521"},
    HostKeyType {LIBSSH2_HOSTKEY_TYPE_ED25519, LIBSSH2_KNOWNHOST_KEY_ED25519, "ssh-ed25519"},
};

std::optional<HostKeyType> findHostKeyType(int sessionType)
{
    for (const auto& type: hostKeyTypes)
    {
        if (type.sessionType == sessionType)
            return type;
    }

    return std::nullopt;
}

void initializeLibssh2Once()
{
    static auto once = std::once_flag {};
    std::call_once(once, [] { libssh2_init(0); });
}

struct SessionDeleter
{
    void operator()(LIBSSH2_SESSION* session) const
    {
        libssh2_session_set_blocking(session, 1);
        libssh2_session_disconnect(session, "CowTerm closed the link");
        libssh2_session_free(session);
    }
};

struct ChannelDeleter
{
    void operator()(LIBSSH2_CHANNEL* channel) const { libssh2_channel_free(channel); }
};

struct KnownHostsDeleter
{
    void operator()(LIBSSH2_KNOWNHOSTS* knownHosts) const { libssh2_knownhost_free(knownHosts); }
};

struct AgentDeleter
{
    void operator()(LIBSSH2_AGENT* agent) const
    {
        libssh2_agent_disconnect(agent);
        libssh2_agent_free(agent);
    }
};

using SessionHandle = std::unique_ptr<LIBSSH2_SESSION, SessionDeleter>;
using ChannelHandle = std::unique_ptr<LIBSSH2_CHANNEL, ChannelDeleter>;
using KnownHostsHandle = std::unique_ptr<LIBSSH2_KNOWNHOSTS, KnownHostsDeleter>;
using AgentHandle = std::unique_ptr<LIBSSH2_AGENT, AgentDeleter>;

struct ResolvedAddresses
{
    ~ResolvedAddresses()
    {
        if (first != nullptr)
            ::freeaddrinfo(first);
    }

    addrinfo* first = nullptr;
};

struct SshSessionRequest
{
    SshDestination destination;
    SshCredentials credentials;
    std::string remoteCommand;
};

// Every ssh failure below reads like OpenSSH's own stderr, so
// describeBridgeFailure words it exactly as it does for the system ssh.
class SshSessionRunner
{
public:
    SshSessionRunner(EmbeddedSshConnection& connectionToUse,
                     SshSessionRequest request,
                     std::function<void(std::string)> onBytesToUse)
        : connection(connectionToUse)
        , destination(std::move(request.destination))
        , credentials(std::move(request.credentials))
        , remoteCommand(std::move(request.remoteCommand))
        , onBytes(std::move(onBytesToUse))
    {
    }

    ~SshSessionRunner()
    {
        channel.reset();
        session.reset();

        if (socketHandle >= 0)
            ::close(socketHandle);
    }

    SessionEnding run()
    {
        const auto steps = std::array {&SshSessionRunner::connectSocket,
                                       &SshSessionRunner::startSession,
                                       &SshSessionRunner::verifyHostKey,
                                       &SshSessionRunner::authenticate,
                                       &SshSessionRunner::openBridgeChannel};

        for (const auto step: steps)
        {
            if (connection.isStopping())
                return {};

            if (auto failure = (this->*step)())
                return {sshFailedExitCode, std::move(*failure)};
        }

        return pumpChannel();
    }

private:
    std::optional<std::string> connectSocket()
    {
        auto hints = addrinfo {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        auto addresses = ResolvedAddresses {};
        const auto port = std::to_string(destination.port);
        const auto resolved =
            ::getaddrinfo(destination.host.c_str(), port.c_str(), &hints, &addresses.first);

        if (resolved != 0)
            return "ssh: Could not resolve hostname " + destination.host + ": " + ::gai_strerror(resolved);

        auto lastError = std::string {timedOut};

        for (auto* address = addresses.first; address != nullptr; address = address->ai_next)
        {
            if (const auto error = connectToAddress(*address))
                lastError = *error;
            else
                return std::nullopt;
        }

        return "ssh: connect to host " + destination.host + " port " + port + ": " + lastError;
    }

    std::optional<std::string> connectToAddress(const addrinfo& address)
    {
        const auto candidate = ::socket(address.ai_family, address.ai_socktype, address.ai_protocol);

        if (candidate < 0)
            return std::strerror(errno);

        if (const auto error = connectWithTimeout(candidate, address))
        {
            ::close(candidate);
            return error;
        }

        socketHandle = candidate;
        return std::nullopt;
    }

    std::optional<std::string> connectWithTimeout(int candidate, const addrinfo& address) const
    {
        const auto noSignalOnBrokenPipe = 1;
        ::setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &noSignalOnBrokenPipe, sizeof(noSignalOnBrokenPipe));

        const auto blockingFlags = ::fcntl(candidate, F_GETFL);
        ::fcntl(candidate, F_SETFL, blockingFlags | O_NONBLOCK);

        if (::connect(candidate, address.ai_addr, address.ai_addrlen) != 0 && errno != EINPROGRESS)
            return std::strerror(errno);

        auto watched = std::array {pollfd {candidate, POLLOUT, 0}, pollfd {connection.wakeReader, POLLIN, 0}};

        if (::poll(watched.data(), watched.size(), setupTimeoutMilliseconds) <= 0)
            return timedOut;

        auto socketError = 0;
        auto errorSize = socklen_t {sizeof(socketError)};
        ::getsockopt(candidate, SOL_SOCKET, SO_ERROR, &socketError, &errorSize);

        if (socketError != 0)
            return std::strerror(socketError);

        ::fcntl(candidate, F_SETFL, blockingFlags);
        return std::nullopt;
    }

    std::optional<std::string> startSession()
    {
        initializeLibssh2Once();
        session.reset(libssh2_session_init());

        if (session == nullptr)
            return "ssh: could not start an ssh session";

        libssh2_session_set_blocking(session.get(), 1);
        libssh2_session_set_timeout(session.get(), setupTimeoutMilliseconds);

        if (libssh2_session_handshake(session.get(), socketHandle) != 0)
            return "ssh: handshake with " + destination.host + " failed: " + describeLastError();

        libssh2_keepalive_config(session.get(), 1, keepaliveIntervalSeconds);
        return std::nullopt;
    }

    std::optional<std::string> verifyHostKey()
    {
        auto keyLength = std::size_t {0};
        auto sessionKeyType = 0;
        const auto* key = libssh2_session_hostkey(session.get(), &keyLength, &sessionKeyType);
        const auto keyType = findHostKeyType(sessionKeyType);

        if (key == nullptr || !keyType)
            return std::string {"ssh: the host offered no key this link can check\n"} + hostKeyVerificationFailed;

        auto knownHosts = readKnownHosts();
        const auto typeMask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | keyType->knownHostType;
        const auto check = libssh2_knownhost_checkp(
            knownHosts.get(), destination.host.c_str(), destination.port, key, keyLength, typeMask, nullptr);

        if (check == LIBSSH2_KNOWNHOST_CHECK_MATCH)
            return std::nullopt;

        if (check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH)
        {
            return "ssh: the " + std::string {keyType->name} + " host key for " + destination.host
                   + " has changed since " + credentials.knownHostsPath + " recorded it\n"
                   + hostKeyVerificationFailed;
        }

        return "ssh: no " + std::string {keyType->name} + " host key is known for " + destination.host
               + " in " + credentials.knownHostsPath + "\n" + hostKeyVerificationFailed;
    }

    // Line by line, so one entry libssh2 cannot parse doesn't lose the rest.
    KnownHostsHandle readKnownHosts() const
    {
        auto knownHosts = KnownHostsHandle {libssh2_knownhost_init(session.get())};
        const auto text = Files::readFile(credentials.knownHostsPath);

        for (const auto& line: Strings::split(text, '\n'))
        {
            if (!line.empty())
                libssh2_knownhost_readline(knownHosts.get(), line.c_str(), line.size(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        }

        return knownHosts;
    }

    std::optional<std::string> authenticate()
    {
        if (authenticateWithAgent() || authenticateWithKeyFile())
            return std::nullopt;

        return destination.user + "@" + destination.host + ": Permission denied (publickey).";
    }

    bool authenticateWithAgent()
    {
        if (credentials.agentSocketPath.empty())
            return false;

        auto agent = AgentHandle {libssh2_agent_init(session.get())};

        if (agent == nullptr)
            return false;

        libssh2_agent_set_identity_path(agent.get(), credentials.agentSocketPath.c_str());

        if (libssh2_agent_connect(agent.get()) != 0 || libssh2_agent_list_identities(agent.get()) != 0)
            return false;

        libssh2_agent_publickey* identity = nullptr;
        libssh2_agent_publickey* previous = nullptr;

        while (libssh2_agent_get_identity(agent.get(), &identity, previous) == 0)
        {
            if (libssh2_agent_userauth(agent.get(), destination.user.c_str(), identity) == 0)
                return true;

            previous = identity;
        }

        return false;
    }

    bool authenticateWithKeyFile()
    {
        if (credentials.privateKeyPath.empty())
            return false;

        const auto userLength = static_cast<unsigned int>(destination.user.size());
        return libssh2_userauth_publickey_fromfile_ex(session.get(),
                                                      destination.user.c_str(),
                                                      userLength,
                                                      nullptr,
                                                      credentials.privateKeyPath.c_str(),
                                                      nullptr)
               == 0;
    }

    std::optional<std::string> openBridgeChannel()
    {
        channel.reset(libssh2_channel_open_session(session.get()));

        if (channel == nullptr || libssh2_channel_exec(channel.get(), remoteCommand.c_str()) != 0)
            return "ssh: could not start ct bridge on " + destination.host + ": " + describeLastError();

        return std::nullopt;
    }

    SessionEnding pumpChannel()
    {
        libssh2_session_set_blocking(session.get(), 0);

        while (!connection.isStopping())
        {
            const auto hasRead = readChannel();
            const auto hasWritten = writeQueuedBytes();

            if (ending)
                return *ending;

            if (libssh2_channel_eof(channel.get()) != 0)
                return finishChannel();

            if (!hasRead && !hasWritten)
                waitForSocketOrWake();
        }

        return {};
    }

    bool readChannel()
    {
        auto hasRead = false;

        while (const auto output = readStream(0))
        {
            onBytes(*output);
            hasRead = true;
        }

        while (const auto error = readStream(SSH_EXTENDED_DATA_STDERR))
        {
            errorOutput += *error;
            hasRead = true;
        }

        return hasRead;
    }

    std::optional<std::string> readStream(int streamId)
    {
        auto buffer = std::array<char, readBufferBytes> {};
        const auto count = libssh2_channel_read_ex(channel.get(), streamId, buffer.data(), buffer.size());

        if (count > 0)
        {
            const auto readBytes = static_cast<std::size_t>(count);
            return std::string {buffer.data(), readBytes};
        }

        if (count < 0 && count != LIBSSH2_ERROR_EAGAIN)
            loseConnection();

        return std::nullopt;
    }

    bool writeQueuedBytes()
    {
        unsentBytes += connection.takeQueuedBytes();

        if (unsentBytes.empty())
            return false;

        const auto written = libssh2_channel_write(channel.get(), unsentBytes.data(), unsentBytes.size());

        if (written == LIBSSH2_ERROR_EAGAIN)
            return false;

        if (written < 0)
        {
            loseConnection();
            return false;
        }

        const auto writtenBytes = static_cast<std::size_t>(written);
        unsentBytes.erase(0, writtenBytes);
        return true;
    }

    void loseConnection()
    {
        if (!ending)
            ending = SessionEnding {sshFailedExitCode, "Connection to " + destination.host + " closed: " + describeLastError()};
    }

    void waitForSocketOrWake()
    {
        auto secondsToNextKeepalive = 0;
        libssh2_keepalive_send(session.get(), &secondsToNextKeepalive);

        const auto directions = libssh2_session_block_directions(session.get());
        const auto isWaitingToSend = (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0;
        const auto eventsToWaitFor = isWaitingToSend ? POLLIN | POLLOUT : POLLIN;
        const auto socketEvents = static_cast<short>(eventsToWaitFor);
        auto watched = std::array {pollfd {socketHandle, socketEvents, 0}, pollfd {connection.wakeReader, POLLIN, 0}};

        const auto waitSeconds = std::max(secondsToNextKeepalive, 1);
        ::poll(watched.data(), watched.size(), waitSeconds * millisecondsPerSecond);
        connection.drainWakes();
    }

    SessionEnding finishChannel()
    {
        libssh2_session_set_blocking(session.get(), 1);
        libssh2_channel_close(channel.get());
        libssh2_channel_wait_closed(channel.get());
        return {libssh2_channel_get_exit_status(channel.get()), errorOutput};
    }

    std::string describeLastError() const
    {
        char* message = nullptr;
        libssh2_session_last_error(session.get(), &message, nullptr, 0);
        return message != nullptr ? std::string {message} : std::string {"unknown error"};
    }

    EmbeddedSshConnection& connection;
    SshDestination destination;
    SshCredentials credentials;
    std::string remoteCommand;
    std::function<void(std::string)> onBytes;
    int socketHandle = -1;
    SessionHandle session;
    ChannelHandle channel;
    std::string unsentBytes;
    std::string errorOutput;
    std::optional<SessionEnding> ending;
};

bool isPortInRange(int port)
{
    constexpr auto highestPort = 65535;
    return port > 0 && port <= highestPort;
}

std::optional<int> parsePort(std::string_view text)
{
    auto port = 0;
    const auto* textEnd = text.data() + text.size();
    const auto [parsedEnd, error] = std::from_chars(text.data(), textEnd, port);

    if (error != std::errc {} || parsedEnd != textEnd)
        return std::nullopt;

    if (!isPortInRange(port))
        return std::nullopt;

    return port;
}
} // namespace

std::optional<SshDestination> parseSshDestination(const std::string& text)
{
    const auto at = text.find('@');

    if (at == std::string::npos || at == 0)
        return std::nullopt;

    auto destination = SshDestination {text.substr(0, at), text.substr(at + 1)};
    const auto colon = destination.host.find(':');

    if (colon != std::string::npos)
    {
        const auto port = parsePort(std::string_view {destination.host}.substr(colon + 1));

        if (!port)
            return std::nullopt;

        destination.port = *port;
        destination.host.resize(colon);
    }

    if (destination.host.empty())
        return std::nullopt;

    return destination;
}

EmbeddedSshLink::EmbeddedSshLink(const SshDestination& destination,
                                 const SshCredentials& credentials,
                                 const std::string& remoteCommand)
    : BridgeLink(destination.user + "@" + destination.host)
    , connection(std::make_shared<EmbeddedSshConnection>())
{
    auto guard = std::weak_ptr<bool> {alive};
    auto deliverBytes = [guard, this](std::string bytes)
    {
        Threads::callAsync([guard, this, bytes = std::move(bytes)]
                           {
                               if (!guard.expired())
                                   receiveBytes(bytes);
                           });
    };

    auto worker = std::thread(
        [shared = connection, request = SshSessionRequest {destination, credentials, remoteCommand}, deliverBytes, guard, this]
        {
            auto ending = SshSessionRunner {*shared, request, deliverBytes}.run();

            if (shared->isStopping())
                return;

            Threads::callAsync([guard, this, ending = std::move(ending)]
                               {
                                   if (!guard.expired())
                                       handleEnd(ending.exitCode, ending.errorOutput);
                               });
        });

    worker.detach();
}

EmbeddedSshLink::~EmbeddedSshLink()
{
    *alive = false;
    connection->stop();
}

void EmbeddedSshLink::writeBytes(const std::string& bytes)
{
    connection->queueBytes(bytes);
}

void EmbeddedSshLink::stopTransport()
{
    connection->stop();
}

void EmbeddedSshLink::handleEnd(int exitCode, const std::string& errorOutput)
{
    if (hasFinished())
        return;

    fail(describeBridgeFailure(getHost(), exitCode, errorOutput));
}
} // namespace term
