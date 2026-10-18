#pragma once

#include <cstddef>
#include <cstdint>

using u64 = std::uint64_t;
using s64 = std::int64_t;
using Result = std::uint32_t;
using ViLayerStack = std::int32_t;

constexpr Result Module_Libnx = 345;
constexpr Result LibnxError_OutOfMemory = 2;
constexpr Result LibnxError_BadInput = 10;
constexpr ViLayerStack ViLayerStack_Default = 0;
constexpr ViLayerStack ViLayerStack_Lcd = 1;
constexpr ViLayerStack ViLayerStack_Screenshot = 2;
constexpr ViLayerStack ViLayerStack_Recording = 3;
constexpr ViLayerStack ViLayerStack_LastFrame = 4;
constexpr ViLayerStack ViLayerStack_ApplicationForDebug = 6;
constexpr std::size_t CAPSSC_JPEG_BUFFER_SIZE = 0x80000;
#define MAKERESULT(module, description) (static_cast<Result>((module) | ((description) << 9)))
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)

extern "C" {
Result capsscInitialize(void);
void capsscExit(void);
Result capsscCaptureJpegScreenShot(u64*, void*, std::size_t, ViLayerStack, s64);
}
