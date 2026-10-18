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

The PC companion adds **Open Breeze**, **Open NRO...**, and **SwitchU status**.
Copy `tools/switchu_remote.py` next to BreezePC's `pcconnect_app.py` and apply
`tools/breezepc-switchu.patch` to add the buttons. The supplied test bundle
already contains the patched PC app. A script can also use:

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
