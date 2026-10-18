#pragma once

#include <cstddef>
#include <cstdint>

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using Result = std::uint32_t;

constexpr Result Module_Libnx = 345;
constexpr Result LibnxError_OutOfMemory = 2;
constexpr Result LibnxError_BadInput = 10;
#define MAKERESULT(module, description) (static_cast<Result>((module) | ((description) << 9)))
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)

constexpr u8 HidDeviceType_FullKey3 = 3;
constexpr u8 HidNpadInterfaceType_Bluetooth = 1;
constexpr u64 HiddbgNpadButton_Home = 1ULL << 18;
constexpr u64 HiddbgNpadButton_Capture = 1ULL << 19;
constexpr u64 HidNpadButton_LeftSL = 1ULL << 24;
constexpr u64 HidNpadButton_LeftSR = 1ULL << 25;
constexpr u64 HidNpadButton_RightSL = 1ULL << 26;
constexpr u64 HidNpadButton_RightSR = 1ULL << 27;
#define RGBA8_MAXALPHA(r, g, b) ((static_cast<u32>(r) << 24) | (static_cast<u32>(g) << 16) | (static_cast<u32>(b) << 8) | 0xFF)

struct HidAnalogStickState { s32 x; s32 y; };
struct HidTouchState {
    u64 delta_time;
    u32 attributes, finger_id, x, y, diameter_x, diameter_y, rotation_angle, reserved;
};
struct HiddbgHdlsSessionId { u64 id; };
struct HiddbgHdlsHandle { u64 handle; };
struct HiddbgHdlsDeviceInfo {
    u8 deviceType;
    u8 npadInterfaceType;
    u8 pad[2];
    u32 singleColorBody;
    u32 singleColorButtons;
    u32 colorLeftGrip;
    u32 colorRightGrip;
};
struct HiddbgHdlsState {
    u32 battery_level;
    u32 flags;
    u64 buttons;
    HidAnalogStickState analog_stick_l;
    HidAnalogStickState analog_stick_r;
    u32 six_axis_sensor_acceleration[3];
    u32 six_axis_sensor_angle[3];
    u32 attribute;
    u8 indicator;
    u8 padding[3];
};

extern "C" {
Result hiddbgInitialize(void);
void hiddbgExit(void);
Result hiddbgSetTouchScreenAutoPilotState(const HidTouchState*, s32);
Result hiddbgUnsetTouchScreenAutoPilotState(void);
Result hiddbgAttachHdlsWorkBuffer(HiddbgHdlsSessionId*, void*, std::size_t);
Result hiddbgReleaseHdlsWorkBuffer(HiddbgHdlsSessionId);
Result hiddbgAttachHdlsVirtualDevice(HiddbgHdlsHandle*, const HiddbgHdlsDeviceInfo*);
Result hiddbgDetachHdlsVirtualDevice(HiddbgHdlsHandle);
Result hiddbgSetHdlsState(HiddbgHdlsHandle, const HiddbgHdlsState*);
void svcSleepThread(u64);
}
