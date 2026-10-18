# BreezePC with SwitchU controls

This folder contains the full patched BreezePC UI and its companion sources,
from the user's Breeze v125.04 package. The UI source matches the version
used for the SwitchU hardware checks (checkout line endings may differ). The precise upstream source commit for
that original package was not recorded. BreezePC originates from
[tomvita/Breeze-Beta](https://github.com/tomvita/Breeze-Beta); GPL-3.0 license
text is supplied in the packages.

Build both downloadable packages from the repository root:

```sh
python tools/package_breezepc.py dist/breezepc
```

- `BreezePC.zip`: complete Python companion, including its launcher and local modules.
- `BreezePC-update.zip`: overwrite these three files in an existing BreezePC folder:
  `pcconnect_app.py`, `switchu_remote.py`, `pc_game_history.py`.

The packager includes the two canonical helper modules from `tools/`; they
are not maintained as duplicate source files in this folder. To run directly
from this source folder, copy those two modules here first.

Install Python 3 with Tk support, then `python -m pip install Pillow capstone`.
Extract `BreezePC.zip` and run `pcconnect_app.bat` on Windows, or
`python pcconnect_app.py`. Existing IP/code settings are reused from
`~/.breeze_pc.json`; settings and credentials are not included in the packages.
Ghidra/Java/pyghidra are optional for the existing decompiler feature and are
not bundled.

**Open Breeze**, **Open NRO**, **SwitchU status**, and **SwitchU controller**
use authenticated TCP 6802 even when Breeze is closed. The daemon update must
be installed and the Switch rebooted first. Game TID start/stop and the
existing Breeze UI/file functions still use Breeze TCP 6801.

The Joy-Con-style controller includes HOME, separate D-pad and left/right
analog pads, L3/R3 stick clicks, and Capture: 26 operations in total,
including the eight stick directions and two center clicks. Rail and SYNC
are outside this main controller layout.
These 26 controls always use SwitchU TCP 6802; clicking them does not probe
6801 or choose a different transport. The original Breeze Game input and
other Breeze functions continue to use 6801. Show/Hide controls Breeze's
overlay without pausing the game. HOME is a real system-button input for
foreground switching and game pause/resume; it is not an overlay Show/Hide.
Each outer-pad click sends a bounded
100 ms analog impulse; positive Y is up and the daemon centers the axes.
HOME, Capture, rail and analog input need the updated daemon. Use transport log
marker **v4** for the controller lifecycle fix: each pulse releases the complete
virtual device/work buffer/service, not only its buttons. The UI hands off
from Breeze Game input before enabling SwitchU input and provides emergency
OFF / explicit enable controls. OFF only cleans up the SwitchU-owned device.
The first d810bfa daemon supports only the original buttons and retains its
controller until the daemon exits; it must be replaced before coexistence testing.

The original NRO/button protocol was tested with daemon `d810bfa`. A loader
`started` response is not an NRO-readiness check; uncertain input is never
automatically replayed. No sys-botbase module is required.

The controller fixes remain a development build pending physical acceptance.
Daemon transport **v5** also supplies the current console JPEG over 6802.
The live picture and Screenshot/F12 prefer it even while Breeze is closed;
an unavailable 6802 channel falls back to the original Breeze capture, with
three seconds between retries. Capture runs independently of the Breeze
menu connection and is capped at ten frames per second. A disconnected
Breeze menu cannot receive clicks using stale button rectangles from an NRO
picture; the independent controller remains available.
Upstream PR #1 is closed. Reopening or submitting a PR requires successful
NRO launch with Breeze closed, HOME, all 26 UI operations, controller cleanup,
emergency OFF, physical-controller reconnection and reboot recovery checks.
