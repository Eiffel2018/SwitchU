#include "remote_nro_capture.hpp"

#include <cstdlib>
#include <cstdint>

namespace switchu::remote_capture {
namespace {

constexpr size_t kJpegBufferAlignment = 0x1000;
constexpr size_t kJpegBufferSize = CAPSSC_JPEG_BUFFER_SIZE;
constexpr s64 kCaptureTimeoutNs = 250'000'000;

Result badInputResult() {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

bool hasJpegEnvelope(const uint8_t* bytes, size_t size) {
    return size >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8
        && bytes[size - 2] == 0xFF && bytes[size - 1] == 0xD9;
}

} // namespace

Result captureJpeg(void** outBuffer, size_t* outSize, ViLayerStack stack) {
    if (outBuffer)
        *outBuffer = nullptr;
    if (outSize)
        *outSize = 0;
    if (!outBuffer || !outSize || (stack != ViLayerStack_Default
        && stack != ViLayerStack_Lcd && stack != ViLayerStack_Screenshot
        && stack != ViLayerStack_Recording && stack != ViLayerStack_LastFrame
        && stack != ViLayerStack_ApplicationForDebug))
        return badInputResult();

    void* buffer = ::aligned_alloc(kJpegBufferAlignment, kJpegBufferSize);
    if (!buffer)
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);

    Result rc = capsscInitialize();
    if (R_FAILED(rc)) {
        std::free(buffer);
        return rc;
    }

    u64 jpegSize = 0;
    rc = capsscCaptureJpegScreenShot(&jpegSize, buffer, kJpegBufferSize,
                                     stack, kCaptureTimeoutNs);
    capsscExit();
    if (R_FAILED(rc)) {
        std::free(buffer);
        return rc;
    }

    if (jpegSize > kJpegBufferSize || jpegSize > SIZE_MAX ||
        !hasJpegEnvelope(static_cast<const uint8_t*>(buffer), static_cast<size_t>(jpegSize))) {
        std::free(buffer);
        return badInputResult();
    }

    *outBuffer = buffer;
    *outSize = static_cast<size_t>(jpegSize);
    return 0;
}

} // namespace switchu::remote_capture
