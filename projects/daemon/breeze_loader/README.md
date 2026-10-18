# Breeze loader

`main` and `main.npdm` are the exefs of Breeze's User Page loader
(`switch/Breeze/profile_18.zip` → `atmosphere/contents/0100000000001013/exefs.nsp`,
the Atmosphere 1.8+ build). It is nx-hbloader set to start
`sdmc:/switch/breeze/breeze.nro`, falling back to `sdmc:/hbmenu.nro`, and its
npdm carries the Album's program id (`0x010000000000100D`).

`xmake install` copies it to
`atmosphere/contents/0100000000001000/breeze_loader/`, next to the daemon, so a
daemon-only install without the SwitchU menu can still start Breeze. With the
`breeze_first` flag set and no User Page loader installed, the daemon registers
this folder as the Album's code for as long as Breeze runs, the same way the
SwitchU menu borrows the Album slot (see `smi::kBreezeAlbumLoaderDir`).

## Launching a requested NRO

The loader source is based on the official
[switchbrew/nx-hbloader v2.4.5](https://github.com/switchbrew/nx-hbloader/tree/82b95122c5ae8dc059bf23893ba7623c72c86773),
pinned at commit `82b95122c5ae8dc059bf23893ba7623c72c86773`. The upstream
license is retained in `upstream/LICENSE.md`. Existing NRO mapping and handoff
remain nx-hbloader's implementation; the fork only adds an optional request
reader before that loader runs.
The descriptor retains the `0x00..0x7F` SVC range supported by SwitchU's
pinned libnx/toolchain; the optional high-SVC ABI hint is therefore zero.

SwitchU writes a one-shot `SwitchUNroLaunchInput` from
`nro_launch_input.h` to `config/SwitchU/nro-launch-request.bin` before starting
the Album loader. Its 528-byte structure contains magic `0x534E524F`, version
1, a nonzero 64-bit request ID, and a NUL-terminated `sdmc:/.../*.nro` path in
a 512-byte field. The loader removes and commits the request file before
opening its target. Common arguments and the Album argument remain untouched.

When no request exists, the loader opens `sdmc:/switch/breeze/breeze.nro` and
falls back to `sdmc:/hbmenu.nro` only when Breeze is missing. An explicit
request never falls back; malformed or failed NRO loads return a nonzero
result. Paths containing spaces are quoted for the loaded NRO's `argv[0]`.

After nx-hbloader validates and maps the requested NRO, it atomically writes a
`SwitchUNroLaunchAck` to `config/SwitchU/nro-launch-ack.bin` immediately before
transferring control to the NRO. The 24-byte acknowledgement repeats the
request ID and includes the loader `Result`. It confirms the loader handoff,
not that the NRO completed initialization. The ack is written through a
temporary file and renamed into place; SwitchU must match the request ID and
clear request, ack, and temporary files before a new launch.

## Rebuilding

With devkitPro and libnx installed, run:

```sh
bash projects/daemon/breeze_loader/build-loader.sh <output-directory>
```

The script builds the pinned source with its checked-in `hbl.json`, extracts
the matching `main` NSO and `main.npdm` from the generated NSP, and checks both
file signatures. `DEVKITPRO` must point to the devkitPro installation. CI
should run this before `xmake install` and package the resulting pair into the
daemon's `breeze_loader` folder.
