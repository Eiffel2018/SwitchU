#include "remote_nro_server.hpp"

#include "remote_nro_protocol.hpp"
#include "remote_nro_input.hpp"
#include <switch.h>
#include <switchu/file_log.hpp>

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

namespace switchu::daemon::remote {
namespace {

constexpr const char* kConfigPath = "sdmc:/switch/Breeze/config.ini";
// Breeze PC connect owns 6801; SwitchU uses its separate fixed endpoint.
constexpr std::uint16_t kDefaultPort = 6802;
constexpr std::size_t kMaxCodeLength = 64;
constexpr std::uint32_t kClientIdlePollLimit = 1200; // 60 seconds at 50 ms
constexpr std::uint32_t kUnauthenticatedIdlePollLimit = 100; // 5 seconds
constexpr const char* kDiagnosticPath = "sdmc:/logs/SwitchU/remote-nro.log";
constexpr long kDiagnosticMaxBytes = 128 * 1024;
std::atomic<bool> g_inputBlocked{false};

struct Settings {
    std::string code;
};

std::mutex g_mutex;
Thread g_thread{};
std::atomic<bool> g_running{false};
bool g_started = false;
bool g_pending = false;
bool g_busy = false;
std::uint64_t g_nextId = 1;
std::uint64_t g_currentId = 0;
std::string g_path;
std::string g_state = "idle";
std::uint32_t g_result = 0;
std::string g_code;
void (*g_wake)() = nullptr;

void diagnostic(const char* event, int fd = -1, int error = 0, Result bsdResult = 0) {
    (void)::mkdir("sdmc:/logs", 0777);
    (void)::mkdir("sdmc:/logs/SwitchU", 0777);
    FILE* file = std::fopen(kDiagnosticPath, "a+");
    if (!file)
        return;
    if (std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) > kDiagnosticMaxBytes) {
        (void)std::fclose(file);
        file = std::fopen(kDiagnosticPath, "w");
        if (!file)
            return;
    }
    std::fprintf(file, "[%s] event=%s fd=%d errno=%d bsd=0x%08X\n",
                 kRemoteChannelVersionMarker, event ? event : "unknown", fd,
                 error, static_cast<unsigned int>(bsdResult));
    (void)std::fclose(file);
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'
                              || value.front() == '\r' || value.front() == '\n'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'
                              || value.back() == '\r' || value.back() == '\n'))
        value.remove_suffix(1);
    return value;
}

unsigned char asciiLower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

bool equalAsciiCaseInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (asciiLower(static_cast<unsigned char>(left[i]))
            != asciiLower(static_cast<unsigned char>(right[i])))
            return false;
    }
    return true;
}

bool validCode(std::string_view code) {
    if (code.size() < 4 || code.size() > kMaxCodeLength)
        return false;
    for (unsigned char c : code) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

bool readSettings(Settings& settings) {
    std::ifstream file(kConfigPath);
    if (!file)
        return false;

    bool inSection = false;
    bool foundSection = false;
    bool foundEnable = false;
    bool enabled = false;
    bool foundCode = false;
    std::string code;
    std::string line;
    while (std::getline(file, line)) {
        if (line.size() > 1024)
            return false;
        std::string_view current = trim(line);
        if (current.empty() || current.front() == '#' || current.front() == ';')
            continue;
        if (current.front() == '[' && current.back() == ']') {
            const std::string_view section = trim(current.substr(1, current.size() - 2));
            inSection = equalAsciiCaseInsensitive(section, "PC connect");
            foundSection = foundSection || inSection;
            continue;
        }
        if (!inSection)
            continue;
        const std::size_t equals = current.find('=');
        if (equals == std::string_view::npos)
            continue;
        const std::string_view key = trim(current.substr(0, equals));
        std::string_view value = trim(current.substr(equals + 1));
        const std::size_t comment = value.find_first_of(";#");
        if (comment != std::string_view::npos)
            value = trim(value.substr(0, comment));

        if (equalAsciiCaseInsensitive(key, "enable")) {
            foundEnable = true;
            enabled = equalAsciiCaseInsensitive(value, "true") || value == "1"
                || equalAsciiCaseInsensitive(value, "yes")
                || equalAsciiCaseInsensitive(value, "on");
        } else if (equalAsciiCaseInsensitive(key, "code")) {
            foundCode = true;
            code.assign(value);
        }
    }
    if (!file.eof() || !foundSection || !foundEnable || !enabled || !foundCode
        || !validCode(code))
        return false;

    settings.code = std::move(code);
    return true;
}

void closeSocket(int fd) {
    if (fd >= 0)
        ::close(fd);
}

void sleepPoll() {
    svcSleepThread(50'000'000ULL);
}

bool sendAll(int fd, std::string_view value, const char* stage) {
    std::size_t sent = 0;
    unsigned int polls = 0;
    while (sent < value.size() && g_running.load() && polls < 40) {
        const ssize_t count = ::send(fd, value.data() + sent, value.size() - sent, 0);
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            polls = 0;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            ++polls;
            svcSleepThread(25'000'000ULL);
        } else {
            const int error = count < 0 ? errno : EPIPE;
            diagnostic(stage, fd, error, count < 0 ? socketGetLastResult() : 0);
            return false;
        }
    }
    if (sent == value.size())
        return true;
    diagnostic(stage, fd, g_running.load() ? ETIMEDOUT : ECANCELED,
               g_running.load() ? socketGetLastResult() : 0);
    return false;
}

void sendStatus(int fd) {
    std::uint64_t id;
    std::string state;
    std::uint32_t result;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        id = g_currentId;
        state = g_state;
        result = g_result;
    }
    char buffer[192];
    const int count = std::snprintf(buffer, sizeof(buffer),
        "OK status id=%llu state=%s rc=0x%08X\n",
        static_cast<unsigned long long>(id), state.c_str(), result);
    if (count > 0 && static_cast<std::size_t>(count) < sizeof(buffer))
        (void)sendAll(fd, std::string_view(buffer, static_cast<std::size_t>(count)),
                      "status-response");
}

enum class ReadOutcome { Line, PeerClosed, Timeout, Stopped, Error, TooLong };

ReadOutcome readLine(int fd, std::string& line, std::uint32_t& idlePolls,
                     std::uint32_t pollLimit, int& readError) {
    line.clear();
    readError = 0;
    char buffer[128];
    while (g_running.load() && idlePolls < pollLimit) {
        const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received == 0)
            return ReadOutcome::PeerClosed;
        if (received < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                readError = errno;
                diagnostic("read-failed", fd, readError, socketGetLastResult());
                return ReadOutcome::Error;
            }
            ++idlePolls;
            sleepPoll();
            continue;
        }
        ++idlePolls;
        for (ssize_t i = 0; i < received; ++i) {
            const char c = buffer[i];
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return ReadOutcome::Line;
            }
            if (line.size() >= kMaxCommandLength) {
                diagnostic("command-too-long", fd);
                return ReadOutcome::TooLong;
            }
            line.push_back(c);
        }
    }
    return g_running.load() ? ReadOutcome::Timeout : ReadOutcome::Stopped;
}

void answerError(int fd, const char* reason) {
    char buffer[64];
    const int count = std::snprintf(buffer, sizeof(buffer), "ERR %s\n", reason);
    if (count > 0 && static_cast<std::size_t>(count) < sizeof(buffer))
        (void)sendAll(fd, std::string_view(buffer, static_cast<std::size_t>(count)),
                      "protocol-error-response");
}

bool queueLaunch(int fd, const std::string& path) {
    std::uint64_t id = 0;
    void (*wake)() = nullptr;
    bool isBusy = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        isBusy = g_busy;
        if (!isBusy) {
            id = g_nextId++;
            if (g_nextId == 0)
                g_nextId = 1;
            g_currentId = id;
            g_path = path;
            g_state = "queued";
            g_result = 0;
            g_pending = true;
            g_busy = true;
            wake = g_wake;
        }
    }
    if (isBusy)
        return sendAll(fd, "ERR busy\n", "busy-response");
    char response[64];
    const int count = std::snprintf(response, sizeof(response), "OK queued %llu\n",
                                    static_cast<unsigned long long>(id));
    const bool sent = count > 0 && static_cast<std::size_t>(count) < sizeof(response)
        && sendAll(fd, std::string_view(response, static_cast<std::size_t>(count)),
                   "launch-response");
    if (wake)
        wake();
    return sent;
}

const char* serveClient(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        diagnostic("accepted-fcntl-get-failed", fd, errno);
        return "fcntl-get-failed";
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        diagnostic("accepted-fcntl-set-failed", fd, errno);
        return "fcntl-set-failed";
    }
    diagnostic("accepted-nonblocking", fd);
    if (!sendAll(fd, "SWITCHU 1\n", "greeting-send"))
        return "greeting-send-failed";
    diagnostic("greeting-sent", fd);

    bool authenticated = false;
    std::uint32_t idlePolls = 0;
    while (g_running.load()) {
        std::string line;
        const std::uint32_t pollLimit = authenticated
            ? kClientIdlePollLimit : kUnauthenticatedIdlePollLimit;
        int readError = 0;
        const ReadOutcome readResult = readLine(fd, line, idlePolls, pollLimit, readError);
        if (readResult != ReadOutcome::Line) {
            if (readResult == ReadOutcome::PeerClosed)
                return "peer-closed";
            if (readResult == ReadOutcome::Timeout) {
                diagnostic("client-timeout", fd, ETIMEDOUT);
                return "client-timeout";
            }
            if (readResult == ReadOutcome::Error)
                return "read-failed";
            if (readResult == ReadOutcome::TooLong)
                return "command-too-long";
            return "server-stopping";
        }
        idlePolls = 0;
        const Command command = parseCommand(line);
        if (command.kind == CommandKind::Auth) {
            if (!constantTimeEqual(command.argument, g_code)) {
                diagnostic("auth-failed", fd);
                (void)sendAll(fd, "ERR auth\n", "auth-error-response");
                return "auth-failed";
            }
            authenticated = true;
            diagnostic("auth-ok", fd);
            if (!sendAll(fd, "OK auth\n", "auth-ok-response"))
                return "auth-response-failed";
            continue;
        }
        if (!authenticated) {
            diagnostic("auth-required", fd);
            (void)sendAll(fd, "ERR auth\n", "auth-required-response");
            return "auth-required";
        }
        if (command.kind == CommandKind::Status) {
            sendStatus(fd);
        } else if (command.kind == CommandKind::Controller) {
            Result rc = 0;
            if (command.argument == "off") {
                g_inputBlocked.store(true);
                rc = switchu::remote_input::shutdown();
                diagnostic("controller-blocked", fd, 0, rc);
            } else if (command.argument == "on") {
                g_inputBlocked.store(false);
                diagnostic("controller-enabled", fd);
            }
            if (R_FAILED(rc)) {
                char response[80];
                const int count = std::snprintf(response, sizeof(response),
                    "ERR controller-cleanup rc=0x%08X\n", rc);
                if (count <= 0 || !sendAll(fd, std::string_view(response, count), "controller-response"))
                    return "controller-response-failed";
            } else if (!sendAll(fd, g_inputBlocked.load()
                ? "OK controller disabled\n" : "OK controller enabled\n", "controller-response")) {
                return "controller-response-failed";
            }
        } else if (command.kind == CommandKind::Launch) {
            if (!queueLaunch(fd, command.argument))
                return "launch-response-failed";
        } else if (command.kind == CommandKind::Press) {
            if (g_inputBlocked.load()) {
                answerError(fd, "input-disabled");
                return "input-blocked";
            }
            // Runs on the dedicated server worker, independent of the NRO.
            // The bounded call releases our own virtual controller before replying.
            const Result rc = switchu::remote_input::pressButton(
                command.buttonMask, command.holdMs);
            char response[96];
            const int count = R_SUCCEEDED(rc)
                ? std::snprintf(response, sizeof(response), "OK pressed %s ms=%u\n",
                                command.argument.c_str(), command.holdMs)
                : std::snprintf(response, sizeof(response), "ERR input rc=0x%08X\n", rc);
            diagnostic(R_SUCCEEDED(rc) ? "input-released" : "input-failed", fd, 0, rc);
            if (count <= 0 || !sendAll(fd, std::string_view(response, count), "input-response"))
                return "input-response-failed";
        } else if (command.kind == CommandKind::Stick) {
            if (g_inputBlocked.load()) {
                answerError(fd, "input-disabled");
                return "input-blocked";
            }
            const Result rc = switchu::remote_input::moveStick(command.argument == "R",
                command.stickX, command.stickY, command.holdMs);
            char response[112];
            const int count = R_SUCCEEDED(rc)
                ? std::snprintf(response, sizeof(response), "OK stick %s x=%d y=%d ms=%u\n",
                    command.argument.c_str(), command.stickX, command.stickY, command.holdMs)
                : std::snprintf(response, sizeof(response), "ERR input rc=0x%08X\n", rc);
            diagnostic(R_SUCCEEDED(rc) ? "stick-centered" : "stick-failed", fd, 0, rc);
            if (count <= 0 || !sendAll(fd, std::string_view(response, count), "stick-response"))
                return "stick-response-failed";
        } else if (command.kind == CommandKind::InvalidPath) {
            answerError(fd, "invalid-path");
        } else {
            answerError(fd, "invalid-command");
        }
        // One authenticated operation per connection. A client holding its
        // socket open cannot monopolize the worker and starve emergency OFF.
        return "request-complete";
    }
    return "server-stopping";
}

int openListener() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        diagnostic("socket-create-failed", -1, errno, socketGetLastResult());
        return -1;
    }
    int yes = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        diagnostic("listener-reuseaddr-failed", fd, errno, socketGetLastResult());
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        diagnostic("listener-fcntl-get-failed", fd, errno);
        closeSocket(fd);
        return -1;
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        diagnostic("listener-fcntl-set-failed", fd, errno);
        closeSocket(fd);
        return -1;
    }
    sockaddr_in address{};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(kDefaultPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0
        || ::listen(fd, 2) < 0) {
        const int error = errno;
        diagnostic("listener-bind-or-listen-failed", fd, error, socketGetLastResult());
        closeSocket(fd);
        switchu::FileLog::log("[remote-nro] listener setup failed errno=%d; retrying", error);
        return -1;
    }
    return fd;
}

void serverThread(void*) {
    bool socketReady = false;
    while (g_running.load()) {
        if (!socketReady) {
            const SocketInitConfig socketConfig = {
                .tcp_tx_buf_size = 0x8000,
                .tcp_rx_buf_size = 0x8000,
                .tcp_tx_buf_max_size = 0x10000,
                .tcp_rx_buf_max_size = 0x10000,
                .udp_tx_buf_size = 0,
                .udp_rx_buf_size = 0,
                .sb_efficiency = 4,
                .num_bsd_sessions = 3,
                .bsd_service_type = BsdServiceType_User,
            };
            const Result rc = socketInitialize(&socketConfig);
            if (R_FAILED(rc)) {
                diagnostic("socket-init-failed", -1, 0, rc);
                switchu::FileLog::log("[remote-nro] socket init failed: 0x%X; retrying", rc);
                for (unsigned int i = 0; i < 40 && g_running.load(); ++i)
                    svcSleepThread(50'000'000ULL);
                continue;
            }
            socketReady = true;
        }

        int listener = openListener();
        if (listener < 0) {
            for (unsigned int i = 0; i < 40 && g_running.load(); ++i)
                svcSleepThread(50'000'000ULL);
            continue;
        }
        diagnostic("listener-ready");
        switchu::FileLog::log("[remote-nro] listener ready port=%u", kDefaultPort);

        while (g_running.load()) {
            sockaddr_in clientAddress{};
            socklen_t addressLength = sizeof(clientAddress);
            const int client = ::accept(listener,
                reinterpret_cast<sockaddr*>(&clientAddress), &addressLength);
            if (client >= 0) {
                diagnostic("accepted", client);
                const char* closeReason = serveClient(client);
                diagnostic(closeReason ? closeReason : "connection-closed", client);
                closeSocket(client);
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                diagnostic("accept-failed", listener, errno, socketGetLastResult());
                switchu::FileLog::log("[remote-nro] accept failed errno=%d; retrying", errno);
                break;
            }
            sleepPoll();
        }
        closeSocket(listener);
    }
    const Result inputCleanup = switchu::remote_input::shutdown();
    if (R_FAILED(inputCleanup))
        diagnostic("input-cleanup-failed", -1, 0, inputCleanup);
    if (socketReady)
        socketExit();
    switchu::FileLog::log("[remote-nro] listener stopped");
}

} // namespace

bool start(void (*wake)()) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_started)
            return true;
    }

    Settings settings;
    if (!readSettings(settings)) {
        switchu::FileLog::log("[remote-nro] disabled or invalid PC connect settings");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_started)
            return true;
        g_code = std::move(settings.code);
        g_wake = wake;
        g_running.store(true);
        Result rc = threadCreate(&g_thread, serverThread, nullptr, nullptr,
                                 0x8000, 0x2E, -2);
        if (R_FAILED(rc)) {
            g_running.store(false);
            g_code.clear();
            g_wake = nullptr;
            switchu::FileLog::log("[remote-nro] threadCreate failed: 0x%X", rc);
            return false;
        }
        rc = threadStart(&g_thread);
        if (R_FAILED(rc)) {
            g_running.store(false);
            threadClose(&g_thread);
            g_code.clear();
            g_wake = nullptr;
            switchu::FileLog::log("[remote-nro] threadStart failed: 0x%X", rc);
            return false;
        }
        g_started = true;
    }
    diagnostic(kRemoteChannelVersionMarker);
    switchu::FileLog::log("[remote-nro] service thread started");
    return true;
}

void stop() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_started)
            return;
        g_running.store(false);
    }
    // The worker uses nonblocking accept/recv/send and bounded sleeps, so it
    // observes shutdown promptly without closing a descriptor from two threads.
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_started = false;
        g_pending = false;
        g_busy = false;
        g_currentId = 0;
        g_path.clear();
        g_state = "idle";
        g_result = 0;
        g_code.clear();
        g_wake = nullptr;
    }
}

bool take(Request& request) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_pending)
        return false;
    request.id = g_currentId;
    request.path = std::move(g_path);
    g_path.clear();
    g_pending = false;
    g_state = "launching";
    return true;
}

bool hasPending() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pending;
}

bool busy() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_busy;
}

void finish(std::uint64_t id, const char* state, std::uint32_t result) {
    if (!state)
        return;
    const std::string_view requested(state);
    if (requested != "started" && requested != "finished" && requested != "failed")
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_currentId != id)
        return;
    g_state.assign(requested);
    g_result = result;
    if (requested == "started" || requested == "finished" || requested == "failed")
        g_busy = false;
}

} // namespace switchu::daemon::remote
