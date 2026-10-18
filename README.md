<div align="center">
    <h1>SwitchU</h1>
    <p>A Wii U-style custom home menu replacement for Nintendo Switch</p>
</div>

<p align="center">
  <a rel="LICENSE" href="https://github.com/PoloNX/SwitchU/blob/master/LICENSE">
    <img src="https://img.shields.io/static/v1?label=license&message=GPLV3&labelColor=111111&color=0057da&style=for-the-badge" alt="License">
  </a>
  <a rel="VERSION" href="https://github.com/PoloNX/SwitchU/releases">
    <img src="https://img.shields.io/static/v1?label=version&message=1.2.0&labelColor=111111&color=06f&style=for-the-badge" alt="Version">
  </a>
  <a rel="BUILD" href="https://github.com/PoloNX/SwitchU/actions">
      <img src="https://img.shields.io/github/actions/workflow/status/PoloNX/SwitchU/switch.yml?branch=master&labelColor=111111&color=06f&style=for-the-badge" alt=Build>
  </a>
</p>

---

- [Screenshots](#screenshots)
  [Installation](#installation)
- [How to build](#how-to-build)
- [SwitchU Manager](#switchu-manager)
- [Help me](#help-me)
- [Credits](#credits)
- [License](#license)


## Starting a game from Breeze PC Connect

With Breeze's HOME toggle enabled (No restart, Fast restart or Overlay), a
`gamestart` request from the identified Breeze applet selects the first user
returned by the system account list. Other launchers retain their user picker.
Games that do not use accounts are launched without a preselected user.

After a successful launch, the daemon waits two seconds, waits for AM to confirm
the foreground transfer, and reopens Breeze. Boot, HOME and automatic returns prefer the
dedicated Album loader bundled with the daemon over a User Page override that
may belong to another homebrew. PC Connect can then reconnect without HOME.
Failed launches are retried after two seconds until Breeze announces it is
running. An in-progress loader is probed every two seconds and gets ten seconds
to cold-start before it is closed and retried; applets are never opened in parallel.
The delay does not block HOME, sleep or game-exit events.
An explicit HOME action, a new launch or a changed application session cancels
the automatic return. Startup applets defer each attempt for up to 15 seconds.
Missing NRO/loader files, foreground failures and loader timeouts are recorded
in `logs/SwitchU/daemon*.log`. A `BreezePC recovery v2` startup line identifies
this build in a newly collected log.

AM does not identify the TCP client: this policy applies to launch requests
from the identified Breeze applet, the path used by BreezePC's Start game.
It requires the normal Breeze NRO and loader installation; it does not install
or enable PC Connect. Both `SwitchU.zip` and `home_daemon.zip` use this daemon.

Host policy check: `g++ -std=c++17 tests/breeze_launch_handoff_test.cpp -o /tmp/breeze-launch-handoff-test && /tmp/breeze-launch-handoff-test`.

## Opening an NRO from BreezePC while Breeze is closed

The HOME daemon provides an independent TCP endpoint on port **6802**. It
reuses `enable` and `code` from `[PC connect]` in
`sdmc:/switch/Breeze/config.ini`; Breeze's own port setting remains unchanged.
The endpoint stays available while Breeze is closed. Missing/disabled PC
Connect settings disable it, and every connection requires the pairing code.

The PC companion adds **Open Breeze**, **Open NRO...**, **SwitchU status**,
and a **SwitchU controller** window. The controller uses the same authenticated
6802 endpoint while Breeze is closed; sys-botbase is not required. Each click
sends one 100 ms button press and releases it. Accepted input does not prove
that an NRO received it (controller assignment and NRO support still apply).
The wire command is `press PLUS 100`, with standard buttons A/B/X/Y,
L/R/ZL/ZR, PLUS/MINUS, LSTICK/RSTICK (L3/R3), HOME/CAPTURE,
LSL/LSR/RSL/RSR (rail buttons), and UP/DOWN/LEFT/RIGHT.
`stick R 0 32767 100` moves the right stick up; use `L` for the left stick.
Axes range from -32768 to 32767, with positive Y up. Holds are bounded to
1–1000 ms and axes return to center before replying; the client never replays
an uncertain input reply. The Joy-Con-style controller separates the D-pad,
left/right sticks and stick clicks. Each stick click outside the center sends
one 100 ms impulse, rather than an indefinitely held axis. HOME and analog
axes, Capture and rail inputs require the updated daemon; the initial d810bfa
daemon does not support them. The two physical SYNC pairing buttons are not
game inputs. The main UI excludes rail/SYNC and provides 26 operations:
24 buttons/directions plus the L3/R3 center clicks. Optional rail protocol
input delivery depends on the virtual controller/style supported by the
target and is not hardware-verified.
The 26 controller operations always use 6802 without an extra 6801 availability
check. Breeze's existing Game input, Show/Hide and other functions retain 6801.
Show/Hide changes the overlay while the game keeps running; HOME switches
foreground between the game and NRO with the corresponding pause/resume.

Transport **v5** adds authenticated `capture`: a JPEG of the current console
display, including other NROs while Breeze is closed. Its reply is
`OK capture bytes=N width=1280 height=720 format=jpeg`, followed by exactly N
binary bytes (at most 512 KiB). This does not press Capture or write an album
file. Python callers use `switchu_remote.capture(host, code)`.
BreezePC's live picture and Screenshot/F12 prefer this 6802 path independently
of the 6801 connection; Breeze-only installations retain the original 6801
capture fallback. Capture is read-only and does not attach a controller.

The controller is shared by the daemon's serialized input worker. Each bounded
operation now neutralizes input, detaches its virtual device, releases its HDLS
work buffer and closes its HID debug service, including failure cleanup. It
does not retain a virtual controller between clicks. The UI stops its existing
Breeze Game input connection before enabling SwitchU controls.
Authenticated `controller off` blocks further SwitchU button/axis input and
cleans up the daemon's controller; `controller on` explicitly enables it again,
and `controller status` reports the gate. OFF does not remove controllers
owned by other processes. There is one authenticated operation per connection,
so an idle client cannot hold the input worker indefinitely. These lifecycle
fixes require daemon transport marker **v4**; do not use the earlier retained
controller implementation when verifying physical controller coexistence.
The [complete patched BreezePC sources and packaging instructions](tools/breezepc/README.md)
are included. `python tools/package_breezepc.py dist/breezepc` creates a complete
`BreezePC.zip` and an overwrite-only `BreezePC-update.zip`; the **BreezePC package**
workflow also publishes both as an artifact. Existing UI source patches remain
available as an alternative. A script can also use:

```sh
python tools/switchu_remote.py SWITCH_IP launch /switch/Breeze/Breeze.nro --wait 20
python tools/switchu_remote.py SWITCH_IP launch /hbmenu.nro --wait 20
python tools/switchu_remote.py SWITCH_IP status
```

The CLI prompts for the existing pairing code; `--code-env VARIABLE` is
available for a local caller. NRO paths are absolute SD paths, optionally
prefixed with `sdmc:`. Spaces are supported; the normalized UTF-8 path must
fit 511 bytes. Files must already exist on the SD card.

SwitchU closes and joins the previous applet, acquires the foreground, then
uses its existing nx-hbloader implementation with a one-shot path request.
The game is suspended while the requested NRO runs in **applet mode**; the
daemon does not terminate it. NROs requiring full application memory need a
different launch mode. A later explicit launch replaces the current applet.

`OK queued N` confirms receipt. Status is `queued`, `launching`, `started`,
`finished`, or `failed` with an ID and Result. `started` confirms that the
loader mapped the NRO and handed control to it; NRO initialization or PC
Connect readiness still requires its own observation. The client polls
status and never automatically repeats an ambiguous launch request.
One-shot request/receipt files are cleared at boot and after the holder
closes, preventing an interrupted launch from replaying. Diagnostics are in
`sdmc:/logs/SwitchU/`; old logs in `config/SwitchU` are left intact.
The endpoint also appends short transport diagnostics to
`sdmc:/logs/SwitchU/remote-nro.log`, opening and closing that file per event so
it can be collected while the daemon is running. Pairing codes are never logged.

## Screenshots

![](./screenshots/1.jpg)

<details>
  <summary><b>More screenshots</b></summary>

![](./screenshots/2.jpg)
![](./screenshots/3.jpg)
![](./screenshots/4.jpg)
![](./screenshots/5.jpg)

</details>

## Installation

Download ``SwitchU.zip`` [here](https://github.com/PoloNX/SwitchU/releases/latest) and extract ``atmosphere`` and ``switch`` folders at the root of your sdcard

## How to build

### Requirements

- [devkitPro](https://devkitpro.org/wiki/Getting_Started)
- [Xmake](https://xmake.io/#/)

### Clone

```bash
git clone --recursive https://github.com/PoloNX/SwitchU
cd SwitchU
```

### Build (production daemon + external menu mode)

```bash
xmake f -p cross --toolchain=devkita64 --homebrew=n --backend=deko3d
xmake
```

### Build (homebrew .nro mode)

```bash
xmake f -p cross --toolchain=devkita64 --homebrew=y --backend=deko3d
xmake
```

### Clean

```bash
xmake clean
```

Build outputs are generated under `build/cross/aarch64/<mode>/`.

## SwitchU Manager

`SwitchU-Manager.nro` enables or disables SwitchU

## Know issues
- Some settings are not implemented yet
- Current icons are very ugly, feel free to replace them with better ones
- You may experience some crash when using a lot of sysmodules. I tried to reduce the ram usage but I still get some instability when launching a game.

## Unsorted roadmap
- [x] Add a pannel when pressing + on a game to show more information about it    
- [x] Add controller/screen test window
- [x] Add Wii U-style folders
- [ ] Improve theme shop UI and metadata
- [ ] Add eshop and news applet  
- [ ] Add a setting to switch the theme based on the time of day  
- [x] Add a steamgriddb integration to get game banners and backgrounds  
- [ ] Replace temporary icons with polished assets
- [ ] Add more animations inspired by Wii U
- [ ] Improve localization coverage

## Help me

If you want to help, open an issue when you find a bug and open a pull request if you have a fix.

## Credits

- Thanks to [Xortroll](https://github.com/Xortroll) for the help and for [uLaunch](https://github.com/Xortroll/uLaunch) which inspired this project a lot

## License

This project is licensed under the GNU General Public License v3.0. See the [LICENSE](https://github.com/PoloNX/SwitchU/blob/master/LICENSE) file for details.
