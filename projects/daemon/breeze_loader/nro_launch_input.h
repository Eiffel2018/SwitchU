#pragma once

#include <stdint.h>

#define SWITCHU_NRO_LAUNCH_MAGIC UINT32_C(0x534E524F)
#define SWITCHU_NRO_LAUNCH_VERSION UINT32_C(1)
#define SWITCHU_NRO_LAUNCH_PATH_SIZE 512
#define SWITCHU_HBMENU_NRO_PATH "sdmc:/hbmenu.nro"
#define SWITCHU_NRO_REQUEST_PATH "sdmc:/config/SwitchU/nro-launch-request.bin"
#define SWITCHU_NRO_ACK_PATH "sdmc:/config/SwitchU/nro-launch-ack.bin"
#define SWITCHU_NRO_ACK_TMP_PATH "sdmc:/config/SwitchU/nro-launch-ack.tmp"

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t requestId;
    char path[SWITCHU_NRO_LAUNCH_PATH_SIZE];
} SwitchUNroLaunchInput;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t requestId;
    uint32_t result;
    uint32_t reserved;
} SwitchUNroLaunchAck;

#if defined(__cplusplus)
static_assert(sizeof(SwitchUNroLaunchInput) == 528, "SwitchU NRO input ABI changed");
static_assert(sizeof(SwitchUNroLaunchAck) == 24, "SwitchU NRO ack ABI changed");
#else
_Static_assert(sizeof(SwitchUNroLaunchInput) == 528, "SwitchU NRO input ABI changed");
_Static_assert(sizeof(SwitchUNroLaunchAck) == 24, "SwitchU NRO ack ABI changed");
#endif
