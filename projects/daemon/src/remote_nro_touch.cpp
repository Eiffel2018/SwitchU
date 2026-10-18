#include "remote_nro_touch.hpp"
#include <mutex>

namespace switchu::remote_touch {
namespace {
std::mutex g_mutex;
bool g_mayOwnTouch = false;
constexpr u64 kReleaseNs = 50'000'000;
constexpr u64 kEdgeNs = 20'000'000;

// hiddbg is initialized by the caller. A failed clear remains marked for a
// later OFF/new-request cleanup; uncertain gestures are never replayed.
Result clearOwned(Result first) {
    if (!g_mayOwnTouch) return first;
    Result rc = hiddbgUnsetTouchScreenAutoPilotState();
    if (R_FAILED(rc)) {
        if (R_SUCCEEDED(first)) first = rc;
        rc = hiddbgUnsetTouchScreenAutoPilotState();
    }
    if (R_SUCCEEDED(rc)) g_mayOwnTouch = false;
    else if (R_SUCCEEDED(first)) first = rc;
    return first;
}
}

Result gesture(u32 x, u32 y, u32 holdMs, bool twice, u32 gapMs) {
    if (x >= 1280 || y >= 720 || holdMs == 0 || holdMs > 5000
        || (twice && (holdMs > 300 || gapMs < 50 || gapMs > 500)))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    std::lock_guard<std::mutex> lock(g_mutex);
    Result rc = hiddbgInitialize();
    if (R_FAILED(rc)) return rc;
    if (g_mayOwnTouch) {
        rc = clearOwned(0);
        if (R_FAILED(rc)) { hiddbgExit(); return rc; }
    }
    HidTouchState point{};
    point.finger_id = 1;
    point.x = x; point.y = y;
    point.diameter_x = point.diameter_y = 10;
    // Publish zero contacts first so the target sees a fresh down transition.
    g_mayOwnTouch = true;
    rc = hiddbgSetTouchScreenAutoPilotState(&point, 0);
    if (R_SUCCEEDED(rc)) svcSleepThread(kReleaseNs);
    bool fingerMayBeDown = false;
    for (unsigned i = 0; R_SUCCEEDED(rc) && i < (twice ? 2U : 1U); ++i) {
        point.attributes = HidTouchAttribute_Start;
        fingerMayBeDown = true;
        rc = hiddbgSetTouchScreenAutoPilotState(&point, 1);
        if (R_FAILED(rc)) break;
        const u64 holdNs = static_cast<u64>(holdMs) * 1'000'000;
        const u64 startNs = holdNs < kEdgeNs ? holdNs : kEdgeNs;
        svcSleepThread(startNs);
        if (holdNs > startNs) {
            point.attributes = 0;
            rc = hiddbgSetTouchScreenAutoPilotState(&point, 1);
            if (R_FAILED(rc)) break;
            svcSleepThread(holdNs - startNs);
        }
        // Some UI consumers activate on the End attribute, not merely on
        // a later zero-contact sample. Publish both edges for a real click.
        point.attributes = HidTouchAttribute_End;
        rc = hiddbgSetTouchScreenAutoPilotState(&point, 1);
        if (R_FAILED(rc)) break;
        svcSleepThread(kEdgeNs);
        rc = hiddbgSetTouchScreenAutoPilotState(&point, 0);
        if (R_SUCCEEDED(rc)) {
            fingerMayBeDown = false;
            svcSleepThread(twice && i == 0
                ? static_cast<u64>(gapMs) * 1'000'000 - kEdgeNs : kReleaseNs);
        }
    }
    if (R_FAILED(rc) && fingerMayBeDown) {
        point.attributes = HidTouchAttribute_End;
        if (R_SUCCEEDED(hiddbgSetTouchScreenAutoPilotState(&point, 1))) svcSleepThread(kEdgeNs);
        if (R_SUCCEEDED(hiddbgSetTouchScreenAutoPilotState(&point, 0))) svcSleepThread(kReleaseNs);
    }
    // Even a failed state IPC may have taken effect: always unset our override.
    rc = clearOwned(rc);
    hiddbgExit();
    return rc;
}

Result shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_mayOwnTouch) return 0;
    Result rc = hiddbgInitialize();
    if (R_FAILED(rc)) return rc;
    rc = clearOwned(0);
    hiddbgExit();
    return rc;
}
}
