#pragma once

#include <cstdint>

namespace switchu::daemon {

// AM does not identify the PC Connect client. Only accept the request while
// an identified Breeze applet is open and no application is running (the
// precondition of PC Connect's gamestart command). Other launchers keep their
// normal user selector, including a game's own ExecuteProgram request.
constexpr bool isBreezeStartRequest(bool appletActive, bool breezeCandidate,
                                    bool breezeIdentified, bool applicationRunning) {
    return appletActive && breezeCandidate && breezeIdentified && !applicationRunning;
}

// A one-shot return to Breeze, tied to the application session that was just
// started. The main loop keeps pumping events during the delay.
class BreezeLaunchHandoff final {
public:
    static constexpr std::uint64_t kDelayNs = 2'000'000'000ULL;
    static constexpr std::uint64_t kTimeoutNs = 15'000'000'000ULL;

    void schedule(std::uint64_t sessionId, std::uint64_t nowNs) {
        m_sessionId = sessionId;
        m_startedAtNs = nowNs;
    }

    void cancel() { m_sessionId = 0; }
    bool pending() const { return m_sessionId != 0; }

    bool due(std::uint64_t sessionId, std::uint64_t nowNs) {
        if (!pending())
            return false;
        const auto elapsed = nowNs - m_startedAtNs;
        if (sessionId != m_sessionId || elapsed >= kTimeoutNs) {
            cancel();
            return false;
        }
        return elapsed >= kDelayNs;
    }

private:
    std::uint64_t m_sessionId = 0;
    std::uint64_t m_startedAtNs = 0;
};

} // namespace switchu::daemon
