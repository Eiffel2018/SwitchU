#pragma once

#include <switch.h>

#include <cstddef>

namespace switchu::remote_capture {

// Captures a 1280x720 JPEG. The screenshot stack excludes non-capture layers;
// explicit stacks allow read-only diagnostics without rebuilding the daemon.
// On success the caller owns *outBuffer and must release it with std::free.
// On every failure outputs are reset to nullptr and 0.
Result captureJpeg(void** outBuffer, size_t* outSize,
                   ViLayerStack stack = ViLayerStack_Screenshot);

} // namespace switchu::remote_capture
