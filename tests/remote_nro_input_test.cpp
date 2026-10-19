#include "../projects/daemon/src/remote_nro_input.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr Result kFailure = -77;
constexpr Result kReleaseFailure = -78;
constexpr Result kOtherFailure = -79;
std::vector<std::string> events;
std::vector<HiddbgHdlsState> states;
std::vector<u64> sleeps;
Result failInitialize = 0;
Result failWorkBufferAttach = 0;
Result failDeviceAttach = 0;
Result failDetach = 0;
Result failRelease = 0;
int failSetCall = 0;
int setCallCount = 0;

void reset() {
    events.clear();
    states.clear();
    sleeps.clear();
    failInitialize = 0;
    failWorkBufferAttach = 0;
    failDeviceAttach = 0;
    failDetach = 0;
    failRelease = 0;
    failSetCall = 0;
    setCallCount = 0;
}

bool containsInOrder(const std::vector<std::string>& expected) {
    std::size_t cursor = 0;
    for (const auto& event : events) {
        if (cursor < expected.size() && event == expected[cursor])
            ++cursor;
    }
    return cursor == expected.size();
}

bool check(bool condition, const char* message) {
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool successButtonPulse() {
    reset();
    const Result rc = switchu::remote_input::pressButton(1ULL << 10, 25);
    return check(rc == 0, "button pulse succeeds")
        && check(containsInOrder({"init", "work+", "device+", "state", "sleep", "state", "device-", "work-", "exit"}), "button pulse releases and destroys owned controller")
        && check(states.size() == 3 && states[0].buttons == 0 && states[1].buttons == (1ULL << 10) && states[2].buttons == 0, "neutral device is published before the short pulse and released before teardown")
        && check(sleeps == std::vector<u64>{1'250'000'000, 25'000'000, 100'000'000}, "device settles before the requested pulse and release is sampled before detach; pulse is not lengthened");
}

bool successStickPulse() {
    reset();
    const Result rc = switchu::remote_input::moveStick(false, -1234, 2345, 40);
    return check(rc == 0, "stick pulse succeeds")
        && check(containsInOrder({"init", "work+", "device+", "state", "sleep", "state", "device-", "work-", "exit"}), "stick pulse releases and destroys owned controller")
        && check(states.size() == 3 && states[0].analog_stick_l.x == 0 && states[0].analog_stick_l.y == 0
            && states[1].analog_stick_l.x == -1234 && states[1].analog_stick_l.y == 2345
            && states[2].analog_stick_l.x == 0 && states[2].analog_stick_l.y == 0, "stick publishes neutral before the impulse and returns to center before teardown");
}

bool initFailureHasNoCleanupCalls() {
    reset();
    failInitialize = kFailure;
    const Result rc = switchu::remote_input::pressButton(1, 10);
    return check(rc == kFailure, "initialize failure is returned")
        && check(events == std::vector<std::string>{"init"}, "no resources are touched after initialize failure");
}

bool stateFailureStillCleansUp() {
    reset();
    failSetCall = 2;
    failDetach = kReleaseFailure;
    failRelease = kOtherFailure;
    const Result rc = switchu::remote_input::pressButton(1, 10);
    return check(rc == kFailure, "state failure is returned")
        && check(containsInOrder({"state", "state", "device-", "work-", "exit"}), "failed state update is neutralized and all resources released");
}

bool neutralFailureStillCleansUp() {
    reset();
    failSetCall = 3;
    const Result rc = switchu::remote_input::pressButton(1, 10);
    return check(rc == kFailure, "neutral-state failure is returned")
        && check(containsInOrder({"state", "sleep", "state", "state", "device-", "work-", "exit"}), "failed neutral update is retried before teardown");
}

bool detachFailureStillReleasesSession() {
    reset();
    failDetach = kFailure;
    failRelease = kReleaseFailure;
    const Result rc = switchu::remote_input::pressButton(1, 10);
    return check(rc == kFailure, "first cleanup failure is returned")
        && check(containsInOrder({"device-", "work-", "exit"}), "detach failure does not retain HDLS work buffer or service");
}

bool releaseFailureStillExitsService() {
    reset();
    failRelease = kReleaseFailure;
    const Result rc = switchu::remote_input::pressButton(1, 10);
    return check(rc == kReleaseFailure, "work-buffer release failure is returned")
        && check(containsInOrder({"device-", "work-", "exit"}), "service exits after work-buffer release was attempted");
}

bool setupFailuresFreeOwnedResources() {
    reset();
    failWorkBufferAttach = kFailure;
    const Result workRc = switchu::remote_input::pressButton(1, 10);
    const bool workClean = check(workRc == kFailure, "work-buffer attach failure is returned")
        && check(containsInOrder({"init", "work+", "exit"}) && events.back() == "exit", "work-buffer attach failure exits service");

    reset();
    failDeviceAttach = kFailure;
    const Result deviceRc = switchu::remote_input::pressButton(1, 10);
    return workClean
        && check(deviceRc == kFailure, "virtual-device attach failure is returned")
        && check(containsInOrder({"init", "work+", "device+", "work-", "exit"}), "device attach failure releases buffer before service exit");
}

bool initialNeutralFailureDoesNotSendPulse() {
    reset();
    failSetCall = 1;
    const Result rc = switchu::remote_input::pressButton(1, 100);
    return check(rc == kFailure && sleeps == std::vector<u64>{100'000'000}, "failed initial publication only settles cleanup neutral; no pressed pulse")
        && check(states.size() == 2 && states[0].buttons == 0 && states[1].buttons == 0,
                 "failed initial publication only retries neutral")
        && check(containsInOrder({"device+", "state", "state", "device-", "work-", "exit"}),
                 "failed initial publication tears down all owned resources");
}

} // namespace

extern "C" {
Result hiddbgInitialize(void) { events.emplace_back("init"); return failInitialize; }
void hiddbgExit(void) { events.emplace_back("exit"); }
Result hiddbgAttachHdlsWorkBuffer(HiddbgHdlsSessionId* session, void* buffer, std::size_t size) {
    events.emplace_back("work+");
    if (buffer == nullptr || (reinterpret_cast<std::uintptr_t>(buffer) & 0xFFF) != 0 || size != 0x1000)
        return -88;
    session->id = 5;
    return failWorkBufferAttach;
}
Result hiddbgReleaseHdlsWorkBuffer(HiddbgHdlsSessionId) { events.emplace_back("work-"); return failRelease; }
Result hiddbgAttachHdlsVirtualDevice(HiddbgHdlsHandle* handle, const HiddbgHdlsDeviceInfo*) {
    events.emplace_back("device+");
    handle->handle = 9;
    return failDeviceAttach;
}
Result hiddbgDetachHdlsVirtualDevice(HiddbgHdlsHandle) { events.emplace_back("device-"); return failDetach; }
Result hiddbgSetHdlsState(HiddbgHdlsHandle, const HiddbgHdlsState* state) {
    events.emplace_back("state");
    states.push_back(*state);
    ++setCallCount;
    return setCallCount == failSetCall ? kFailure : 0;
}
void svcSleepThread(u64 ns) { events.emplace_back("sleep"); sleeps.push_back(ns); }
}

int main() {
    bool ok = true;
    ok = successButtonPulse() && ok;
    reset();
    ok = check(switchu::remote_input::pressButton(0x303, 100) == 0,
               "four simultaneous buttons accepted") && ok;
    ok = check(states.size() == 3 && states[0].buttons == 0 && states[1].buttons == 0x303
               && states[2].buttons == 0, "one chord state then all buttons released") && ok;
    reset();
    ok = check(switchu::remote_input::pressButton(0x1F, 100) != 0
               && switchu::remote_input::pressButton((1ULL << 18) | 1, 100) != 0
               && events.empty(), "invalid chord does not attach a controller") && ok;
    ok = successStickPulse() && ok;
    ok = initFailureHasNoCleanupCalls() && ok;
    ok = stateFailureStillCleansUp() && ok;
    ok = neutralFailureStillCleansUp() && ok;
    ok = detachFailureStillReleasesSession() && ok;
    ok = releaseFailureStillExitsService() && ok;
    ok = setupFailuresFreeOwnedResources() && ok;
    ok = initialNeutralFailureDoesNotSendPulse() && ok;
    if (!ok)
        return EXIT_FAILURE;
    std::cout << "remote NRO input host tests passed\n";
    return EXIT_SUCCESS;
}
