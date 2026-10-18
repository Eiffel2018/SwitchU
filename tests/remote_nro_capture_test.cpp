#include "../projects/daemon/src/remote_nro_capture.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr Result kFailure = 77;
std::vector<std::string> events;
Result initResult = 0;
Result captureResult = 0;
u64 reportedSize = 8;
bool corruptJpeg = false;
void* captureBuffer = nullptr;
size_t captureBufferSize = 0;
ViLayerStack captureStack = -1;
s64 captureTimeout = 0;

void reset() {
    events.clear();
    initResult = 0;
    captureResult = 0;
    reportedSize = 8;
    corruptJpeg = false;
    captureBuffer = nullptr;
    captureBufferSize = 0;
    captureStack = -1;
    captureTimeout = 0;
}

bool check(bool condition, const char* message) {
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool successCapturesAndReleasesService() {
    reset();
    void* output = reinterpret_cast<void*>(1);
    size_t size = 99;
    const Result rc = switchu::remote_capture::captureJpeg(&output, &size);
    const bool ok = check(rc == 0, "valid capture succeeds")
        && check(output != nullptr && size == reportedSize, "JPEG buffer and size are returned")
        && check((reinterpret_cast<std::uintptr_t>(captureBuffer) & 0xFFF) == 0, "buffer is page aligned")
        && check(captureBufferSize == CAPSSC_JPEG_BUFFER_SIZE, "capture uses the documented maximum buffer")
        && check(captureStack == ViLayerStack_Default && captureTimeout == 250'000'000, "default layer stack and bounded timeout are used")
        && check(events == std::vector<std::string>{"init", "capture", "exit"}, "service is closed after every successful request");
    std::free(output);
    return ok;
}

bool initializeFailureResetsOutputs() {
    reset();
    initResult = kFailure;
    void* output = reinterpret_cast<void*>(1);
    size_t size = 99;
    const Result rc = switchu::remote_capture::captureJpeg(&output, &size);
    return check(rc == kFailure, "initialization failure is returned")
        && check(output == nullptr && size == 0, "initialization failure resets outputs")
        && check(events == std::vector<std::string>{"init"}, "no capture or exit after failed initialization");
}

bool captureFailureClosesServiceAndResetsOutputs() {
    reset();
    captureResult = kFailure;
    void* output = reinterpret_cast<void*>(1);
    size_t size = 99;
    const Result rc = switchu::remote_capture::captureJpeg(&output, &size);
    return check(rc == kFailure, "capture failure is returned")
        && check(output == nullptr && size == 0, "capture failure resets outputs")
        && check(events == std::vector<std::string>{"init", "capture", "exit"}, "service exits after capture failure");
}

bool malformedLengthsAndMarkersAreRejected() {
    reset();
    reportedSize = 3;
    void* output = nullptr;
    size_t size = 0;
    Result rc = switchu::remote_capture::captureJpeg(&output, &size);
    const bool shortRejected = check(R_FAILED(rc) && output == nullptr && size == 0, "short JPEG is rejected and freed");

    reset();
    reportedSize = CAPSSC_JPEG_BUFFER_SIZE + 1;
    rc = switchu::remote_capture::captureJpeg(&output, &size);
    const bool oversizedRejected = check(R_FAILED(rc) && output == nullptr && size == 0, "oversized JPEG is rejected and freed");

    reset();
    corruptJpeg = true;
    rc = switchu::remote_capture::captureJpeg(&output, &size);
    return shortRejected && oversizedRejected
        && check(R_FAILED(rc) && output == nullptr && size == 0, "invalid JPEG markers are rejected and freed");
}

bool invalidArgumentsAreRejected() {
    reset();
    size_t size = 99;
    Result rc = switchu::remote_capture::captureJpeg(nullptr, &size);
    const bool nullBuffer = check(R_FAILED(rc) && size == 0 && events.empty(), "null buffer output is rejected before service init");
    reset();
    void* output = reinterpret_cast<void*>(1);
    rc = switchu::remote_capture::captureJpeg(&output, nullptr);
    return nullBuffer && check(R_FAILED(rc) && output == nullptr && events.empty(), "null size output is rejected before service init");
}

} // namespace

extern "C" {
Result capsscInitialize(void) { events.emplace_back("init"); return initResult; }
void capsscExit(void) { events.emplace_back("exit"); }
Result capsscCaptureJpegScreenShot(u64* jpegSize, void* buffer, std::size_t bufferSize,
                                  ViLayerStack layerStack, s64 timeout) {
    events.emplace_back("capture");
    captureBuffer = buffer;
    captureBufferSize = bufferSize;
    captureStack = layerStack;
    captureTimeout = timeout;
    if (R_FAILED(captureResult))
        return captureResult;
    auto* bytes = static_cast<std::uint8_t*>(buffer);
    if (!corruptJpeg && bufferSize >= 8) {
        const std::array<std::uint8_t, 8> jpeg{0xFF, 0xD8, 0x01, 0x02, 0x03, 0x04, 0xFF, 0xD9};
        std::memcpy(bytes, jpeg.data(), jpeg.size());
    } else if (corruptJpeg && bufferSize >= 8) {
        std::memset(bytes, 0, 8);
    }
    *jpegSize = reportedSize;
    return 0;
}
}

int main() {
    bool ok = true;
    ok = successCapturesAndReleasesService() && ok;
    ok = initializeFailureResetsOutputs() && ok;
    ok = captureFailureClosesServiceAndResetsOutputs() && ok;
    ok = malformedLengthsAndMarkersAreRejected() && ok;
    ok = invalidArgumentsAreRejected() && ok;
    if (!ok)
        return EXIT_FAILURE;
    std::cout << "remote NRO capture host tests passed\n";
    return EXIT_SUCCESS;
}
