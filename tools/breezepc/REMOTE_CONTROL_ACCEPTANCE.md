# Remote control hardware acceptance — 2026-10-18

Daemon source: `68bb0c4`. [Release build and host checks](https://github.com/HY-Leung/SwitchU/actions/runs/39118405440) passed. The six installed SD files were read back through Breeze and checked with SHA-256 before reboot. The console runs Breeze beta125.05; the included PC sources originated from the user's v125.04 package.

## Results

| Check | Result |
| --- | --- |
| Independent NRO launch | Pass, authenticated 6802; Probe controls work with Breeze closed, then Breeze is launched from Probe |
| A, B, X, Y, L, R, ZL, ZR, +, −, D-pad up/down/left/right, L3, R3 | All 16 pass, individual HID traces and neutral release |
| Left and right stick up/down/left/right | All 8 pass, correct signed axes and centering |
| HOME | Pass, game and same retained Probe; increasing frame counter, unchanged holder |
| Capture | Pass, new album JPEG read back and visually confirmed as Probe |
| Actual BreezePC controls | Pass, real Tk button/canvas callbacks and real 6802 requests, every pulse 100 ms |
| Game 100 ms direction input | Pass, 12 alternating DOWN/UP operations; exactly one menu item per operation |
| Game 100 ms A/B | Pass, Settings opens and returns to the main menu |
| First HOME synchronization | Pass, Overlay → No restart → replace Breeze; first HOME shows game, second returns to same Breeze UID/game PID |
| Input while Breeze is hidden | Pass, X in the game does not launch Breeze's ftpsrv action |
| Overlay compatibility | Pass, HOME hide and original 6801 show/hide; original Overlay/pause-Yes settings restored |
| Emergency OFF / explicit resume | Pass, disabled UI sends no input; direct input rejected; resume acknowledged |
| Cleanup | Pass, neutral states, gate OFF, UI workers stopped, Breeze gamepad detached and no stopped debugger/BPs |
| Reboot recovery | Pass, one Reboot NRO PLUS, disconnect, fresh daemon id=0/idle and Breeze reachable |
| Physical controller after virtual input | Pass, human-confirmed X with SwitchU input OFF; Probe recorded X and neutral release |

The 24 ordinary controls were observed inside the Probe. HOME and Capture were verified separately by their system-visible effects, giving 26 distinct UI operations. Probe trace intervals show the requested 100 ms pressed state; the virtual device is discovered at neutral for 1.25 seconds first, and remains neutral for 100 ms after release. A click takes about 1.45 seconds plus network overhead.

## Evidence and limits

Local receipts and screenshots are under `dist/breezepc-input-home-68bb0c4/`: `validation.json`, `sd-install-receipt.json`, `post-reboot.json`, `ui26-acceptance.json`, `game-nav-acceptance.json`, `game-a-acceptance.json`, `first-home-acceptance.json`, `overlay-acceptance.json`, `physical-reconnect.json` and `final-cleanup.json`. Earlier failing builds and screenshots remain separate; their failures are not counted as passes. Final state: Breeze main menu, SwitchU input OFF, Breeze gamepad detached, no stopped debugger/BPs and no retained client.

This verifies the tested console, Probe and Graveyard Keeper 2 main menu. It does not establish compatibility with every game's controller assignment rules. Virtual input does not merge into a connected physical P1. OFF only releases SwitchU-owned resources; another process's controller is unaffected. The same NRO applet slot is reused, not a second simultaneous NRO. TID start/stop, SD access and original Breeze features retain 6801. Pairing codes and user settings are excluded from packages and evidence.

## Touch extension — 2026-10-19

Installed daemon: `f7a802a`, built by [native CI 39139602085](https://github.com/HY-Leung/SwitchU/actions/runs/39139602085). Final daemon/Probe production sources match that successful build; subsequent changes affect PC handling/documentation. The seven SD payloads, including the updated read-only Probe, passed SHA-256 readback and reboot recovery. PC functional source: `6e0afdb` (later commits update documentation).

| Check | Result |
| --- | --- |
| Touch API: corners, interior coordinate, double, 800 ms and 5000 ms long | Pass; observed HID contacts, held frames and zero-contact release |
| Double tap | Two down/up pairs at the requested interior coordinate; 80 ms holds and 120 ms lifted gap checked against Probe frames |
| Screen-edge coordinates | HOS clamps X to 15–1264 and Y to 15–704; interior coordinates arrive unchanged. Replies echo the request. Initial exact-corner failure retained as diagnostic evidence |
| Actual PC coordinate controls and preview gestures | Pass; real Tk callbacks and actual console requests for tap/double/long, including single/double coalescing and hold/release |
| OFF during a 5000 ms long press | Pass; UI waited about 5.08 seconds, received disabled, and Probe reported zero contacts. PC OFF timeout is 10 seconds |
| Real NRO action | A double gesture on hbmenu's Breeze tile launched Breeze with no A command |
| Breeze interaction | Pass with raw 6802 touch only: first tap selected Save, second opened Main; already selected Settings opened on one tap, and Save returned to Main. No A or button RPC |
| Existing 26 controls | All 24 ordinary controls observed/released; HOME showed game then the same Probe; Capture album JPEG read back and visually confirmed |
| Cleanup | SwitchU input OFF, no touch/virtual device retained, Breeze gamepad detached, no stopped debugger/BPs, all owned client/UI workers closed |
| Python and native checks | 36 protocol/helper plus 12 PC exclusion/capture tests passed; native gesture timing/cleanup fault tests, original input and protocol tests passed |

Final receipts: `dist/breezepc-touch-final-f7a802a/` (`touch-api-live-calibrated.json`, `touch-ui-live.json`, `touch-off-long-live.json`, `hbmenu-double-launch.json`, `touch-breeze-confirm.json`, `ui26-acceptance.json`, `final-cleanup.json`, `sd-install-receipt.json`, `post-reboot.json`). Earlier cohorts remain separately preserved. The Start/End attribute experiment did not propagate those flags on this HOS and was reverted; it is not part of the installed daemon or accepted timing result.

Touch uses `hiddbgSetTouchScreenAutoPilotState` with explicit zero contacts and unset afterward, not HDLS gamepad allocation. It temporarily overrides touch input during the bounded gesture. It is single-finger input; multi-touch, drag, persistent touch sessions and acquire/release leases are outside this extension. A long gesture serializes the 6802 worker, so callers should await its reply before capture/status and budget timeout for its duration. These checks prove the observed raw input and named application responses, not identical gesture handling in every NRO.

In Breeze beta125.05, an unselected button is highlighted on the first tap and activated on the next; an already selected button activates on one tap. Both paths passed without an A command (`touch-breeze-followup.json`). Other NROs may interpret touch differently.


## 2026-10-19: hbmenu separation, direct TID start and chords

Native source `5f77070` (CI run `39189002879`), PC functionality `2d1cff4` (PC package run `39189722058`), Breeze beta125.05. Daemon and bundled loader were installed together, all six SD files/package verified by SHA-256 readback, then rebooted. Evidence cohort: `dist/6802-game-chords/`.

- Cold boot showed actual hbmenu. The preceding `cc10acd` cohort also verified DBI exit and Breeze B exit return to hbmenu. Explicit 6802 Breeze launch restored original 6801 access; no hbmenu-to-Breeze default substitution remains.
- From cold-boot hbmenu with Breeze closed, 6802 `gamestart 0100005027A18000` launched GK2 with the first system account and no input/player-selector confirmation. Around 5 seconds later Breeze 6801 was ready; the game reached its title menu in front and no save was loaded. A noninstalled TID failed without closing hbmenu. Starting while GK2 ran failed busy and preserved its PID.
- The archived daemon log confirms the two-second return schedule, background launchable-event timeout, foreground fallback, `game resume rc=0`, and completed Breeze handshake. This console used the permitted foreground-open-then-resume path; this is not proof of true background launch support on another HOS. The new readiness guard avoids the SDK wait observed in the earlier candidate.
- Actual Tk UI regression: all 24 normal/stick operations were observed and released in Probe; HOME showed GK2 then the same Probe. CAPTURE created a new console album JPEG, read back and visually verified (2026101910350000, 282828 bytes). OFF rejected input, explicit resume worked, and all owned workers stopped.
- `L+R`, `ZL+ZR+A`, and `ZL+ZR+A+B` produced simultaneous 2/3/4-bit masks in single Probe frames, followed by neutral release. The existing temporary-controller lifetime and first-pulse settle delay remain. More than four/duplicate keys, and HOME/CAPTURE/rail mixed chords, are rejected. No persistent controller or lease was added.
- Native protocol/input/runner/touch/capture host checks and release build passed; 31 Python transport checks, 7 history/dialog checks and real Tk mock-transport callbacks passed. ZIP CRC, Python compilation and CI/local source comparison passed. No separate new physical-controller reconnect claim is made; that human check remains recorded in the earlier cohorts.

After testing GK2 was stopped without loading a save, a second reboot archived the new log, and Breeze was opened for handoff. Final state: no game/debugger/BP, Breeze gamepad detached, SwitchU input OFF, root sockets/UI workers closed; console formally returned to the Graveyard Keeper task. Its mod/NPDM/save files were not changed. XCOM2 is excluded from future tests at the user's request.

The original 6801 game/file/debug commands remain available. The new PC Start menu uses 6802 and never resends an ambiguous start through 6801; callers must stop a running game first. A queue/started receipt alone does not certify application readiness or every application's handling of a chord.


## 2026-10-20: upstream 1.2.0n / Breeze beta126 interface 5

Source `10e083aa56e29e2a6e10dbf80a7ff6123dbfe00a` merges upstream `43cdac4` into the standalone 6802 fork. Native CI `39399007661` passed, including eight host fixtures. The daemon-only ZIP passed CRC, five-member manifest, interface/version and NSO/NPDM identity checks. Module SHA256: `6ce356dbcc6a0d01a293afdf9021fb074c7477ce6bc0bba46db23d036255ff5e`. Loader remains `c5194782d44d3ea076f52357f9eff9dbd826fa4ae42cde9b40bd3d46863101de`, NPDM `f526ecc43f94ec256dc1bca1161f24293790010fcf9962e70119bf1337eb3bb8`.

Merge resolutions retain generated loader/toolbox installation in a daemon-only build and make the interface-5 return one-shot: an explicit NRO replacement or requested game launch cancels it. Breeze identity is retained while its system-applet request is consumed. Existing 6802 commands and PC source functionality are unchanged.

Targeted beta126.00 hardware window granted by the NSHome owner, 17:29:15-17:39:15 HKT:

- Paired candidate install/readback, NSHome boot flag temporarily disabled, normal reboot to real hbmenu.
- Actual Breeze Nintendo applets menu opened Mii Maker. Cancel returned to Breeze Games without creating a Mii. This checks a real system applet; it does not claim all five applets were exercised.
- Album follows this console's hbmenu override. While that requested applet was open, explicit 6802 `/hbmenu.nro` replacement stayed in hbmenu without an unwanted Breeze return. Stock Album was not verified.
- Games Back (page 2) returned to Breeze Main, then normal B exited to hbmenu. No Breeze-as-default regression.
- Optional GK2 6802 start selected its user automatically; 6801 returned with PID143/BIDbc64bd880d0cde99. One `gamestop` returned stopped and left Breeze visible, without the stock software-closed prompt. This was a startup-only test, not a loaded-save or full title-menu test. No debug attach/stop/BP/watch or cheat changes were made. The stale `attached:true` UI bit briefly remained without PID/TID; `save status` reported no game and the restored boot reported attached:false. Stop was not replayed.
- Restored original SwitchU loader/metadata, left the original `7b40...` daemon disabled, re-enabled NSHome boot2 and normal-rebooted. Verified stock qlaunch, fresh NSHome0.1.11 boot141931730, module hash unchanged, Breeze6801 ready, game/debug/gamepad empty and input OFF. Removed owned temporary files and closed clients, returning before the deadline. The candidate is NOT left installed on the shared console.

Evidence: `C:/Users/OP/AppData/Local/Temp/switchu-interface5/` (candidate.json, install/restore receipts, mii-return.json, screenshots, final-return.json). Eden lab has no relevant 6802/HOS cross-process adapter, so this targeted integration check used an explicit physical window after PC fixtures; no emulator profile was started. R:/Breeze_v126 and NSHome/tools/breezepc were not modified.
