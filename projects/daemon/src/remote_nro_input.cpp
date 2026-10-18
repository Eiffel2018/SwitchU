#include "remote_nro_input.hpp"

#include <cstdlib>
#include <mutex>

namespace switchu::remote_input {
namespace {

constexpr u32 kMaxHoldMs = 1000;
constexpr size_t kWorkBufferAlignment = 0x1000;
constexpr size_t kWorkBufferSize = 0x1000;
constexpr u64 kAllowedButtons = 0xFFFFULL | HiddbgNpadButton_Home | HiddbgNpadButton_Capture
    | HidNpadButton_LeftSL | HidNpadButton_LeftSR | HidNpadButton_RightSL | HidNpadButton_RightSR;
static_assert(HiddbgNpadButton_Home == (1ULL << 18));
static_assert(HiddbgNpadButton_Capture == (1ULL << 19));
static_assert(HidNpadButton_LeftSL == (1ULL << 24) && HidNpadButton_RightSR == (1ULL << 27));

std::mutex g_mutex;
bool g_initialized = false;
bool g_hiddbgReady = false;
bool g_workBufferAttached = false;
bool g_deviceAttached = false;
bool g_stateMayBeActive = false;
HiddbgHdlsSessionId g_session{};
HiddbgHdlsHandle g_handle{};
void* g_workBuffer = nullptr;

bool isOneAllowedButton(u64 mask) {
    return mask != 0 && (mask & ~kAllowedButtons) == 0 && (mask & (mask - 1)) == 0;
}

HiddbgHdlsDeviceInfo makeDeviceInfo() {
    HiddbgHdlsDeviceInfo info{};
    info.deviceType = HidDeviceType_FullKey3;
    info.npadInterfaceType = HidNpadInterfaceType_Bluetooth;
    info.singleColorBody = RGBA8_MAXALPHA(255, 255, 255);
    info.singleColorButtons = RGBA8_MAXALPHA(0, 0, 0);
    info.colorLeftGrip = RGBA8_MAXALPHA(230, 255, 0);
    info.colorRightGrip = RGBA8_MAXALPHA(0, 40, 20);
    return info;
}

HiddbgHdlsState makeState(u64 buttons) {
    HiddbgHdlsState state{};
    state.battery_level = 4;
    state.buttons = buttons;
    return state;
}

// Caller holds g_mutex. Always tear down every resource we acquired, even if
// neutralization or detaching our own virtual device fails.
Result cleanupOwnedControllerLocked(Result firstResult) {
    if (g_deviceAttached && g_stateMayBeActive) {
        const HiddbgHdlsState neutral = makeState(0);
        const Result neutralResult = hiddbgSetHdlsState(g_handle, &neutral);
        if (R_SUCCEEDED(firstResult) && R_FAILED(neutralResult))
            firstResult = neutralResult;
        g_stateMayBeActive = false;
    }

    if (g_deviceAttached) {
        const Result detachResult = hiddbgDetachHdlsVirtualDevice(g_handle);
        if (R_SUCCEEDED(firstResult) && R_FAILED(detachResult))
            firstResult = detachResult;
        // Continue through work-buffer/service release even when detach fails.
        g_deviceAttached = false;
    }

    if (g_workBufferAttached) {
        const Result releaseResult = hiddbgReleaseHdlsWorkBuffer(g_session);
        if (R_SUCCEEDED(firstResult) && R_FAILED(releaseResult))
            firstResult = releaseResult;
        // libnx closes the transfer-memory handle before returning, including
        // the service-error path, so only now is the backing allocation freed.
        std::free(g_workBuffer);
        g_workBuffer = nullptr;
        g_workBufferAttached = false;
    } else if (g_workBuffer) {
        // Attach failed; libnx closes its transfer-memory handle on that path.
        std::free(g_workBuffer);
        g_workBuffer = nullptr;
    }

    if (g_hiddbgReady) {
        hiddbgExit();
        g_hiddbgReady = false;
    }
    g_initialized = false;
    g_stateMayBeActive = false;
    return firstResult;
}

Result initializeController() {
    if (g_initialized)
        return 0;

    Result result = hiddbgInitialize();
    if (R_FAILED(result))
        return result;
    g_hiddbgReady = true;

    g_workBuffer = ::aligned_alloc(kWorkBufferAlignment, kWorkBufferSize);
    if (!g_workBuffer)
        return cleanupOwnedControllerLocked(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));

    result = hiddbgAttachHdlsWorkBuffer(&g_session, g_workBuffer, kWorkBufferSize);
    if (R_FAILED(result))
        return cleanupOwnedControllerLocked(result);
    g_workBufferAttached = true;

    const HiddbgHdlsDeviceInfo info = makeDeviceInfo();
    result = hiddbgAttachHdlsVirtualDevice(&g_handle, &info);
    if (R_FAILED(result))
        return cleanupOwnedControllerLocked(result);

    g_deviceAttached = true;
    g_initialized = true;
    // Give hid time to publish the new controller before the first state update.
    svcSleepThread(100'000'000ULL);
    return 0;
}

Result applyStateFor(const HiddbgHdlsState& pressed, u32 holdMs) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Result result = initializeController();
    if (R_FAILED(result))
        return result;

    // Treat a failed update as possibly applied and always clear it before
    // detaching this transient virtual controller.
    g_stateMayBeActive = true;
    result = hiddbgSetHdlsState(g_handle, &pressed);
    if (R_SUCCEEDED(result)) {
        svcSleepThread(static_cast<u64>(holdMs) * 1'000'000ULL);
        const HiddbgHdlsState released = makeState(0);
        result = hiddbgSetHdlsState(g_handle, &released);
        if (R_SUCCEEDED(result))
            g_stateMayBeActive = false;
    }
    return cleanupOwnedControllerLocked(result);
}

} // namespace

Result pressButton(u64 button, u32 holdMs) {
    if (!isOneAllowedButton(button) || holdMs == 0 || holdMs > kMaxHoldMs)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return applyStateFor(makeState(button), holdMs);
}

Result moveStick(bool right, s32 x, s32 y, u32 holdMs) {
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767
        || holdMs == 0 || holdMs > kMaxHoldMs)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    HiddbgHdlsState state = makeState(0);
    auto& stick = right ? state.analog_stick_r : state.analog_stick_l;
    stick.x = x;
    stick.y = y;
    return applyStateFor(state, holdMs);
}

Result shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized && !g_hiddbgReady && !g_workBufferAttached && !g_deviceAttached && !g_workBuffer)
        return 0;
    return cleanupOwnedControllerLocked(0);
}

} // namespace switchu::remote_input
