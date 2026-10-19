#include "../projects/daemon/breeze_loader/nro_launch_policy.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    char path[512] = {0}, argv[2048] = "old arguments";
    assert(switchuDefaultNextNro(path, sizeof(path), argv, sizeof(argv)));
    assert(strcmp(path, "sdmc:/hbmenu.nro") == 0);
    assert(strcmp(argv, path) == 0);

    // An explicit request or hbmenu envSetNextLoad must survive unchanged.
    strcpy(path, "sdmc:/switch/DBI/DBI.nro");
    strcpy(argv, "\"sdmc:/switch/DBI/DBI.nro\" custom");
    assert(switchuDefaultNextNro(path, sizeof(path), argv, sizeof(argv)));
    assert(strcmp(path, "sdmc:/switch/DBI/DBI.nro") == 0);
    assert(strcmp(argv, "\"sdmc:/switch/DBI/DBI.nro\" custom") == 0);

    // Same loader process: Breeze exit -> hbmenu -> DBI -> hbmenu.
    strcpy(path, "sdmc:/switch/Breeze/Breeze.nro");
    assert(switchuDefaultNextNro(path, sizeof(path), argv, sizeof(argv)));
    assert(strcmp(path, "sdmc:/switch/Breeze/Breeze.nro") == 0);
    for (int i = 0; i < 3; ++i) {
        path[0] = '\0'; // nx-hbloader clears this before entering each NRO.
        assert(switchuDefaultNextNro(path, sizeof(path), argv, sizeof(argv)));
        assert(strcmp(path, "sdmc:/hbmenu.nro") == 0);
    }

    // Bad buffers must fail without a partial path or stale argv rewrite.
    path[0] = '\0';
    strcpy(argv, "unchanged");
    assert(!switchuDefaultNextNro(path, 2, argv, sizeof(argv)));
    assert(path[0] == '\0' && strcmp(argv, "unchanged") == 0);
    assert(!switchuDefaultNextNro(path, sizeof(path), argv, 2));
    assert(!switchuDefaultNextNro(NULL, 0, argv, sizeof(argv)));
    puts("Default hbmenu, explicit NRO, chainload and return checks passed");
}
