#include "../projects/daemon/src/breeze_launch_handoff.hpp"

#include <cassert>
#include <iostream>

using switchu::daemon::BreezeLaunchHandoff;
using switchu::daemon::isBreezeStartRequest;

int main() {
    // A live, identified Breeze applet with no game is the PC start path.
    assert(isBreezeStartRequest(true, true, true, false));
    assert(!isBreezeStartRequest(false, true, true, false));
    assert(!isBreezeStartRequest(true, false, true, false));
    assert(!isBreezeStartRequest(true, true, false, false));
    // A game can request another program while Breeze is held behind it.
    assert(!isBreezeStartRequest(true, true, true, true));

    BreezeLaunchHandoff handoff;
    assert(!handoff.pending());
    assert(!handoff.due(1, 0));

    handoff.schedule(1, 0);
    assert(handoff.pending());
    assert(!handoff.due(1, BreezeLaunchHandoff::kDelayNs - 1));
    assert(handoff.due(1, BreezeLaunchHandoff::kDelayNs));
    handoff.cancel(); // the daemon consumes the return before attempting it
    assert(!handoff.due(1, BreezeLaunchHandoff::kDelayNs + 1));

    // A replacement application invalidates the old return, even with the
    // same title/PID. Session identity, not just Title ID, fences the handoff.
    handoff.schedule(2, 100);
    assert(!handoff.due(3, 100 + BreezeLaunchHandoff::kDelayNs));
    assert(!handoff.pending());

    // Startup applets may keep the foreground busy beyond two seconds. The
    // return remains due while waiting, but expires instead of polling forever.
    handoff.schedule(4, 100);
    assert(handoff.due(4, 100 + BreezeLaunchHandoff::kDelayNs));
    assert(handoff.due(4, 100 + BreezeLaunchHandoff::kTimeoutNs - 1));
    assert(!handoff.due(4, 100 + BreezeLaunchHandoff::kTimeoutNs));
    assert(!handoff.pending());

    // A new request starts its own delay; it cannot inherit an old deadline.
    handoff.schedule(5, 0);
    handoff.schedule(6, BreezeLaunchHandoff::kDelayNs);
    assert(!handoff.due(6, BreezeLaunchHandoff::kDelayNs));
    assert(handoff.due(6, BreezeLaunchHandoff::kDelayNs * 2));

    // HOME, sleep, failed launch and game exit cancel rather than reopen.
    handoff.cancel();
    assert(!handoff.pending());
    assert(!handoff.due(6, BreezeLaunchHandoff::kDelayNs * 2));
    handoff.schedule(0, 0); // no valid application session
    assert(!handoff.pending());

    std::cout << "Breeze launch source and delayed handoff checks passed\n";
}
