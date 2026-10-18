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
