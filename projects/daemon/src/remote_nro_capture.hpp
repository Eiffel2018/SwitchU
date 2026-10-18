#pragma once

#include <switch.h>

#include <cstddef>

namespace switchu::remote_capture {

// Captures the complete default layer stack as a 1280x720 JPEG.
// On success the caller owns *outBuffer and must release it with std::free.
// On every failure outputs are reset to nullptr and 0.
Result captureJpeg(void** outBuffer, size_t* outSize);

} // namespace switchu::remote_capture
