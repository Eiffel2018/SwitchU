#pragma once

#include <switch.h>

namespace switchu::remote_input {

// Lazily creates a virtual controller owned by this module, then performs one
// bounded press. The returned libnx Result is the first failure, or 0.
// Only ordinary game/UI buttons are accepted; HOME/Capture and stick axes
// are intentionally outside this API.
Result pressButton(HidNpadButton button, u32 holdMs);

// Releases all buttons and destroys only this module's virtual controller.
Result shutdown();

} // namespace switchu::remote_input
