#include "remote_nro_server.hpp"

#include "remote_nro_protocol.hpp"
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

struct Settings {
    std::string code;
    std::uint16_t port = kDefaultPort;
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
std::uint16_t g_port = kDefaultPort;
void (*g_wake)() = nullptr;

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
    settings.port = kDefaultPort;
    return true;
}

void closeSocket(int fd) {
    if (fd >= 0)
        ::close(fd);
}

void sleepPoll() {
    svcSleepThread(50'000'000ULL);
}

bool sendAll(int fd, std::string_view value) {
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
            return false;
        }
    }
    return sent == value.size();
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
        (void)sendAll(fd, std::string_view(buffer, static_cast<std::size_t>(count)));
}

bool readLine(int fd, std::string& line, std::uint32_t& idlePolls,
              std::uint32_t pollLimit) {
    line.clear();
    char buffer[128];
    while (g_running.load() && idlePolls < pollLimit) {
        const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received == 0)
            return false;
        if (received < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                return false;
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
                return true;
            }
            if (line.size() >= kMaxCommandLength)
                return false;
            line.push_back(c);
        }
    }
    return false;
}

void answerError(int fd, const char* reason) {
    char buffer[64];
    const int count = std::snprintf(buffer, sizeof(buffer), "ERR %s\n", reason);
    if (count > 0 && static_cast<std::size_t>(count) < sizeof(buffer))
        (void)sendAll(fd, std::string_view(buffer, static_cast<std::size_t>(count)));
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
        return sendAll(fd, "ERR busy\n");
    char response[64];
    const int count = std::snprintf(response, sizeof(response), "OK queued %llu\n",
                                    static_cast<unsigned long long>(id));
    const bool sent = count > 0 && static_cast<std::size_t>(count) < sizeof(response)
        && sendAll(fd, std::string_view(response, static_cast<std::size_t>(count)));
    if (wake)
        wake();
    return sent;
}

void serveClient(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (!sendAll(fd, "SWITCHU 1\n"))
        return;

    bool authenticated = false;
    std::uint32_t idlePolls = 0;
    while (g_running.load()) {
        std::string line;
        const std::uint32_t pollLimit = authenticated
            ? kClientIdlePollLimit : kUnauthenticatedIdlePollLimit;
        if (!readLine(fd, line, idlePolls, pollLimit))
            break;
        idlePolls = 0;
        const Command command = parseCommand(line);
        if (command.kind == CommandKind::Auth) {
            if (!constantTimeEqual(command.argument, g_code)) {
                (void)sendAll(fd, "ERR auth\n");
                break;
            }
            authenticated = true;
            if (!sendAll(fd, "OK auth\n"))
                break;
            continue;
        }
        if (!authenticated) {
            (void)sendAll(fd, "ERR auth\n");
            break;
        }
        if (command.kind == CommandKind::Status) {
            sendStatus(fd);
        } else if (command.kind == CommandKind::Launch) {
            if (!queueLaunch(fd, command.argument))
                break;
        } else if (command.kind == CommandKind::InvalidPath) {
            answerError(fd, "invalid-path");
        } else {
            answerError(fd, "invalid-command");
        }
    }
}

int openListener() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int yes = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        closeSocket(fd);
        return -1;
    }
    sockaddr_in address{};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(g_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0
        || ::listen(fd, 2) < 0) {
        const int error = errno;
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
                .tcp_tx_buf_size = 0x4000,
                .tcp_rx_buf_size = 0x4000,
                .tcp_tx_buf_max_size = 0,
                .tcp_rx_buf_max_size = 0,
                .udp_tx_buf_size = 0,
                .udp_rx_buf_size = 0,
                .sb_efficiency = 1,
                .num_bsd_sessions = 3,
                .bsd_service_type = BsdServiceType_User,
            };
            const Result rc = socketInitialize(&socketConfig);
            if (R_FAILED(rc)) {
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
        switchu::FileLog::log("[remote-nro] listener ready port=%u", g_port);

        while (g_running.load()) {
            sockaddr_in clientAddress{};
            socklen_t addressLength = sizeof(clientAddress);
            const int client = ::accept(listener,
                reinterpret_cast<sockaddr*>(&clientAddress), &addressLength);
            if (client >= 0) {
                serveClient(client);
                closeSocket(client);
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                switchu::FileLog::log("[remote-nro] accept failed errno=%d; retrying", errno);
                break;
            }
            sleepPoll();
        }
        closeSocket(listener);
    }
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
        g_port = settings.port;
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
