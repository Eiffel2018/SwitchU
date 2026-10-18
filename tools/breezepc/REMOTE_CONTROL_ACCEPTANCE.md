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
| Breeze interaction | Injected touch selected Settings; a separate 6802 A confirmed it and opened Settings. No implicit A or button-ID RPC is added to raw touch |
| Existing 26 controls | All 24 ordinary controls observed/released; HOME showed game then the same Probe; Capture album JPEG read back and visually confirmed |
| Cleanup | SwitchU input OFF, no touch/virtual device retained, Breeze gamepad detached, no stopped debugger/BPs, all owned client/UI workers closed |
| Python and native checks | 36 protocol/helper plus 12 PC exclusion/capture tests passed; native gesture timing/cleanup fault tests, original input and protocol tests passed |

Final receipts: `dist/breezepc-touch-final-f7a802a/` (`touch-api-live-calibrated.json`, `touch-ui-live.json`, `touch-off-long-live.json`, `hbmenu-double-launch.json`, `touch-breeze-confirm.json`, `ui26-acceptance.json`, `final-cleanup.json`, `sd-install-receipt.json`, `post-reboot.json`). Earlier cohorts remain separately preserved. The Start/End attribute experiment did not propagate those flags on this HOS and was reverted; it is not part of the installed daemon or accepted timing result.

Touch uses `hiddbgSetTouchScreenAutoPilotState` with explicit zero contacts and unset afterward, not HDLS gamepad allocation. It temporarily overrides touch input during the bounded gesture. It is single-finger input; multi-touch, drag, persistent touch sessions and acquire/release leases are outside this extension. A long gesture serializes the 6802 worker, so callers should await its reply before capture/status and budget timeout for its duration. These checks prove the observed raw input and named application responses, not identical gesture handling in every NRO.
