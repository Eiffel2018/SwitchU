#pragma once

#include <switch.h>

namespace switchu::remote_input {

// Lazily creates a virtual controller owned by this module, then performs one
// bounded press. The returned libnx Result is the first failure, or 0.
// Accepts ordinary game/UI buttons and the hiddbg HOME bit; never Capture.
Result pressButton(u64 button, u32 holdMs);

// Bounded analog impulse; positive Y is up. Both axes return to center before
// replying, including a best-effort neutral update on a failed state call.
Result moveStick(bool right, s32 x, s32 y, u32 holdMs);

// Releases all buttons and destroys only this module's virtual controller.
Result shutdown();

} // namespace switchu::remote_input
