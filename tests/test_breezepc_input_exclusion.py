import threading
import unittest
from pathlib import Path
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "breezepc"))
sys.path.insert(0, str(ROOT / "tools"))
import pcconnect_app
from pcconnect_app import App


class FakeStatus:
    def __init__(self):
        self.text = ""

    def config(self, **kwargs):
        self.text = kwargs.get("text", self.text)


class FakeVar:
    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value

    def set(self, value):
        self.value = value


class FakeRoot:
    def __init__(self):
        self.callbacks = []

    def after(self, delay, callback):
        self.callbacks.append((delay, callback))


class FakePopup:
    def winfo_exists(self):
        return True


class FakePad:
    def __init__(self, alive=True, cleanup_known=False):
        self.alive = alive
        self.cleanup_known = cleanup_known
        self.cleanup_error = "Breeze release was not acknowledged"
        self.stop = threading.Event()

    def is_alive(self):
        return self.alive


class FakeWorker:
    instances = []

    def __init__(self, *, target, args, daemon):
        self.target, self.args, self.daemon = target, args, daemon
        self.started = False
        self.instances.append(self)

    def start(self):
        self.started = True


def app_with_pad(pad):
    app = object.__new__(App)
    app.root = FakeRoot()
    app.status = FakeStatus()
    app.game_var = FakeVar(True)
    app.pad = pad
    app.pad_stopping = None
    app.switchu_controller = FakePopup()
    app.switchu_waiting_for_pad = False
    app.switchu_pad_deadline = 0
    app.switchu_controller_state = None
    app.switchu_controller_command_pending = False
    app.switchu_busy = False
    app.switchu_generation = 0
    app.cfg = {"host": "127.0.0.1", "code": "pair"}
    app._set_switchu_buttons = lambda: None
    app.enable_calls = 0
    app._maybe_enable_switchu_controller = lambda: setattr(app, "enable_calls", app.enable_calls + 1)
    return app


class BreezeSwitchUExclusionTests(unittest.TestCase):
    def test_switchu_enables_only_after_padlink_exit_and_acknowledged_release(self):
        pad = FakePad(alive=True)
        app = app_with_pad(pad)

        app._begin_switchu_pad_handoff()
        self.assertFalse(app.game_var.get())
        self.assertIsNone(app.pad)
        self.assertTrue(pad.stop.is_set())
        self.assertTrue(app.switchu_waiting_for_pad)
        self.assertEqual(app.enable_calls, 0)

        pad.alive = False
        pad.cleanup_known = True
        app._poll_switchu_pad_handoff()
        self.assertFalse(app.switchu_waiting_for_pad)
        self.assertIsNone(app.pad_stopping)
        self.assertEqual(app.enable_calls, 1)

    def test_timeout_keeps_switchu_input_blocked_without_enabling_controller(self):
        pad = FakePad(alive=True)
        app = app_with_pad(pad)
        app._begin_switchu_pad_handoff()
        app.switchu_pad_deadline = 0

        app._poll_switchu_pad_handoff()
        self.assertFalse(app.switchu_waiting_for_pad)
        self.assertEqual(app.enable_calls, 0)
        self.assertIn("did not stop", app.status.text)
        self.assertFalse(app._switchu_pad_ready())

    def test_exit_without_release_ack_keeps_switchu_input_blocked(self):
        pad = FakePad(alive=False, cleanup_known=False)
        app = app_with_pad(pad)
        app._begin_switchu_pad_handoff()

        self.assertFalse(app.switchu_waiting_for_pad)
        self.assertEqual(app.enable_calls, 0)
        self.assertIn("did not confirm", app.status.text)
        self.assertFalse(app._switchu_pad_ready())

    def test_emergency_off_is_allowed_while_breeze_padlink_is_stuck(self):
        app = app_with_pad(FakePad(alive=True))
        FakeWorker.instances = []
        with patch.object(pcconnect_app.threading, "Thread", FakeWorker):
            app.stop_switchu_controller()

        self.assertTrue(app.switchu_controller_command_pending)
        self.assertEqual(len(FakeWorker.instances), 1)
        worker = FakeWorker.instances[0]
        self.assertTrue(worker.started)
        self.assertIs(worker.args[3], False)


if __name__ == "__main__":
    unittest.main()
