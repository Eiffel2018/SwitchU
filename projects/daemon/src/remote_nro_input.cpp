#include "remote_nro_input.hpp"

#include <cstdlib>
#include <mutex>

namespace switchu::remote_input {
namespace {

constexpr u32 kMaxHoldMs = 1000;
constexpr size_t kWorkBufferAlignment = 0x1000;
constexpr size_t kWorkBufferSize = 0x1000;
constexpr u64 kAllowedButtons = 0xFFFFULL; // A/B/X/Y, sticks, shoulders, +/- and D-pad.

std::mutex g_mutex;
bool g_initialized = false;
bool g_workBufferAttached = false;
bool g_deviceAttached = false;
HiddbgHdlsSessionId g_session{};
HiddbgHdlsHandle g_handle{};
void* g_workBuffer = nullptr;

bool isOneAllowedButton(HidNpadButton button) {
    const u64 mask = static_cast<u64>(button);
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

Result initializeController() {
    if (g_initialized)
        return 0;

    Result result = hiddbgInitialize();
    if (R_FAILED(result))
        return result;

    g_workBuffer = ::aligned_alloc(kWorkBufferAlignment, kWorkBufferSize);
    if (!g_workBuffer) {
        hiddbgExit();
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    result = hiddbgAttachHdlsWorkBuffer(&g_session, g_workBuffer, kWorkBufferSize);
    if (R_FAILED(result)) {
        std::free(g_workBuffer);
        g_workBuffer = nullptr;
        hiddbgExit();
        return result;
    }
    g_workBufferAttached = true;

    const HiddbgHdlsDeviceInfo info = makeDeviceInfo();
    result = hiddbgAttachHdlsVirtualDevice(&g_handle, &info);
    if (R_FAILED(result)) {
        hiddbgReleaseHdlsWorkBuffer(g_session);
        g_workBufferAttached = false;
        std::free(g_workBuffer);
        g_workBuffer = nullptr;
        hiddbgExit();
        return result;
    }

    g_deviceAttached = true;
    g_initialized = true;
    // Give hid time to publish the new controller before the first state update.
    svcSleepThread(100'000'000ULL);
    return 0;
}

} // namespace

Result pressButton(HidNpadButton button, u32 holdMs) {
    if (!isOneAllowedButton(button) || holdMs == 0 || holdMs > kMaxHoldMs)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    std::lock_guard<std::mutex> lock(g_mutex);
    Result result = initializeController();
    if (R_FAILED(result))
        return result;

    const HiddbgHdlsState pressed = makeState(static_cast<u64>(button));
    result = hiddbgSetHdlsState(g_handle, &pressed);
    if (R_SUCCEEDED(result)) {
        svcSleepThread(static_cast<u64>(holdMs) * 1'000'000ULL);
        const HiddbgHdlsState released = makeState(0);
        result = hiddbgSetHdlsState(g_handle, &released);
    }

    // A failed state update may have been partially applied; clear our state.
    if (R_FAILED(result)) {
        const HiddbgHdlsState released = makeState(0);
        hiddbgSetHdlsState(g_handle, &released);
    }
    return result;
}

Result shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized)
        return 0;

    Result result = 0;
    if (g_deviceAttached) {
        const HiddbgHdlsState released = makeState(0);
        const Result stateResult = hiddbgSetHdlsState(g_handle, &released);
        if (R_FAILED(stateResult))
            result = stateResult;
        const Result detachResult = hiddbgDetachHdlsVirtualDevice(g_handle);
        if (R_FAILED(detachResult)) {
            if (R_SUCCEEDED(result))
                result = detachResult;
            return result;
        }
        g_deviceAttached = false;
    }

    if (g_workBufferAttached) {
        const Result releaseResult = hiddbgReleaseHdlsWorkBuffer(g_session);
        if (R_FAILED(releaseResult)) {
            if (R_SUCCEEDED(result))
                result = releaseResult;
        }
        // libnx closes the transfer-memory handle before this call returns,
        // including its service-error path, so the backing memory is now safe
        // to free.
        std::free(g_workBuffer);
        g_workBuffer = nullptr;
        g_workBufferAttached = false;
    }

    hiddbgExit();
    g_initialized = false;
    return result;
}

} // namespace switchu::remote_input
