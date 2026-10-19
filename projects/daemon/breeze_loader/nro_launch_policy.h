#pragma once

#include "nro_launch_input.h"
#include <stddef.h>
#include <string.h>

// nx-hbloader clears nextPath before entering an NRO. A normal return must
// therefore go to hbmenu, while envSetNextLoad/one-shot requests keep their
// explicit target. Never substitute Breeze for an empty or missing target.
static inline int switchuDefaultNextNro(char *nextPath, size_t pathSize,
                                      char *nextArgv, size_t argvSize) {
    if (!nextPath || !nextArgv || pathSize == 0 || argvSize == 0)
        return 0;
    if (nextPath[0] != '\0')
        return 1;
    if (pathSize < sizeof(SWITCHU_HBMENU_NRO_PATH) ||
        argvSize < sizeof(SWITCHU_HBMENU_NRO_PATH))
        return 0;
    memcpy(nextPath, SWITCHU_HBMENU_NRO_PATH, sizeof(SWITCHU_HBMENU_NRO_PATH));
    memcpy(nextArgv, SWITCHU_HBMENU_NRO_PATH, sizeof(SWITCHU_HBMENU_NRO_PATH));
    return 1;
}
