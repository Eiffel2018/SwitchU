# SwitchU Controller Probe

A small homebrew diagnostic NRO for observing ordinary libnx HID input. It
does not create virtual controllers or inject input. Use `controller-probe.nro`
from the SD card; it displays the current held buttons and stick axes.

The read-only TCP service listens on port 6803. It reads the existing
`[PC connect] enable` and `code` values from `sdmc:/switch/Breeze/config.ini`.
If the section is absent, disabled, or invalid, the listener stays closed.
It never displays or logs the code.

Wire protocol (one snapshot request per authenticated connection):

```text
PROBE 1
auth CODE
OK auth
snapshot
{"frame":...,"buttons":"0x...","lx":...,"ly":...,"rx":...,"ry":...,"trace":[...]}
```

The trace retains the latest 128 frame changes. Each authenticated connection
is limited to 64 input-loop frames. Build with the standard devkitPro libnx
Makefile. This directory is licensed under GPL-3.0-only; see the repository
root `LICENSE`.
