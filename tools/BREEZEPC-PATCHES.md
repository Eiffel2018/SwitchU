# BreezePC source patches

For ready-to-run sources and complete/update ZIP packages, use
[`breezepc/README.md`](breezepc/README.md). Patching an old UI is optional.

Start from `pcconnect_app.py.original` (the BreezePC version before the recent-game chooser). Apply `breezepc-game-history.patch` first, then `breezepc-switchu.patch`. The first patch adds the recent-game chooser and also updates the launch status text to match the daemon's automatic Breeze-restore behavior.

Copy `pc_game_history.py` and `switchu_remote.py` beside `pcconnect_app.py`; the patches only modify the app source. `test_pc_game_history.py` checks the history module and, when `R:\Breeze_v125.04` exists, the chooser behavior.
