#pragma once
#include <switch.h>

namespace switchu::remote_touch {
// Single-finger screen gesture, with explicit lift and autopilot teardown.
// Coordinates use the 1280x720 capture; no virtual gamepad is attached.
Result gesture(u32 x, u32 y, u32 holdMs, bool twice, u32 gapMs);
Result shutdown();
}
