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
Game start keeps automatic first-user selection and the two-second Breeze
return for PC reconnection. The daemon first tries a true background launch;
if unavailable it uses a foreground compatibility launch and returns the
game foreground when Breeze is ready.
Boot/exit defaults to the installed SwitchU menu or hbmenu, independently of
Breeze; this requires the matching daemon/loader update and reboot. Explicit
other-NRO launches cancel pending game-start returns.

**Start game → Enter TID...** accepts a nonzero 16-digit hexadecimal title ID
(with an optional `0x` prefix), even when the recent-game list is empty.
Accepted start requests are remembered in that list; observed game names can
replace the bare TID later. Invalid input and Cancel send no start request.

The Joy-Con-style controller includes HOME, separate D-pad and left/right
analog pads, L3/R3 stick clicks, and Capture: 26 operations in total,
including the eight stick directions and two center clicks. Rail and SYNC
are outside this main controller layout.
These 26 controls always use SwitchU TCP 6802; clicking them does not probe
6801 or choose a different transport. The original Breeze Game input and
other Breeze functions continue to use 6801. Show/Hide controls Breeze's
overlay and retains its existing `Overlay pauses game` setting. HOME follows
Breeze's selected HOME mode: No restart switches game/NRO foreground;
Overlay toggles the overlay.
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

The 100 ms pulse starts after a 1.25 second neutral discovery period. The
daemon then publishes neutral for 100 ms before releasing its device. Each
100 ms operation takes about 1.45 seconds plus transport overhead; the button
itself is pressed for 100 ms. No virtual controller is retained between clicks.
The first-HOME handoff fix synchronizes confirmed foreground before replacing
the NRO; it does not require a preparatory HOME press after replacement.

Hardware acceptance: all 26 UI controls, twelve repeated 100 ms game-menu
moves, A/B game-menu actions, first HOME after a mode change/replacement,
Overlay/No restart, screenshots, emergency OFF and reboot recovery passed.
Physical-controller reconnection also passed: the user pressed X after
SwitchU input was disabled, and Probe recorded the physical X and release.
Daemon transport **v5** also supplies the current console JPEG over 6802.
The live picture and Screenshot/F12 prefer it even while Breeze is closed;
an unavailable 6802 channel falls back to the original Breeze capture, with
three seconds between retries. Capture runs independently of the Breeze
menu connection and is capped at ten frames per second. A disconnected
Breeze menu cannot receive clicks using stale button rectangles from an NRO
picture; the independent controller remains available.
The complete protocol/acceptance scope is recorded in
[REMOTE_CONTROL_ACCEPTANCE.md](REMOTE_CONTROL_ACCEPTANCE.md), also included
in both downloadable packages.

## Touch extension

SwitchU controls now include X/Y coordinates (1280×720), tap/double/long mode,
hold milliseconds, double-tap gap and **Send Touch**. The tested console
clamps the outer 15-pixel border (X 15–1264,
Y 15–704); interior coordinates are unchanged. Replies echo the request, not
an observed HID position. The same explicit input enable/OFF gate applies.
Enable **Touch preview via 6802** to click the picture,
double-click, or hold then release. A single click waits 550 ms so a double
click becomes one double-tap request. Clicks outside the picture and dragging
are rejected; original Breeze mouse/menu behavior is preserved when disabled.

Tap holds: 1–300 ms. Double: two tap holds with a 50–500 ms lifted gap.
Long: 300–5000 ms (default 800). The helper adjusts timeout to the hold length.
Each gesture releases touch autopilot without attaching a virtual gamepad.
`controller off` covers touch cleanup as well. This is single-finger input,
not multi-touch or a client-held touch session. OFF waits for the bounded
gesture to finish and allows 10 seconds for its reply. A 5-second hold followed
by OFF has passed a real UI/console test. Touch creates no virtual gamepad.

Touch gestures and all 26 original controls passed console regression checks.
In the tested Breeze beta125.05 build, a first tap selects an unselected
button and a second tap opens it; an already selected button opens on one tap.
Both paths passed raw 6802 touch without A. A double gesture on hbmenu's
Breeze tile also launched Breeze without A. Applications may
interpret the same raw touch differently; no extra A or 6801 button command
is inserted into a gesture. No acquire/release lease was added.

### Candidate: direct game start and chords on 6802

Start game (history or Enter TID) uses authenticated `gamestart TID` on 6802 without requiring Breeze. The TID must be 16 nonzero hexadecimal digits and an installed application record. A running game is rejected, rather than silently replaced. Status uses the same request ID as NRO launch: queued/launching/started/failed. `started` confirms the application launch calls succeeded, not that its title screen or save loaded. First system account selection and the session-bound two-second Breeze restore are retained. No automatic retry or fallback to 6801 follows an unknown outcome; original 6801 commands remain supported.

SwitchU controls also provides a Chord field: 2–4 distinct ordinary buttons, e.g. `L+R` or `ZL+ZR+A+B`. The daemon applies one combined mask to one virtual controller for 100 ms, then releases every button and detaches as before. HOME/Capture/rail operations remain individual commands. This does not add a persistent P1 device. Requires the matching candidate daemon; physical acceptance is pending.
