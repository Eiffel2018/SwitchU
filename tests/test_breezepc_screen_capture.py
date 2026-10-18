import queue
import threading
import unittest
from pathlib import Path
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "breezepc"))
sys.path.insert(0, str(ROOT / "tools"))

import pcconnect_app
import switchu_remote


class FakeLink:
    def __init__(self, capture_on=True):
        self.capture_lock = threading.Lock()
        self.capture_on = threading.Event()
        if capture_on:
            self.capture_on.set()
        self.picture = True
        self.fps = 6.0
        self.jobs = []
        self.connected = threading.Event()

    def send(self, line):
        self.jobs.append(line)


class FakeRecorder:
    def __init__(self):
        self.frames = []

    def frame(self, data):
        self.frames.append(data)


class FakeCanvas:
    def focus_set(self):
        pass


class FakeApp:
    def __init__(self):
        self.cfg = {"host": "192.0.2.1", "code": "pair"}
        self.link = FakeLink()
        self.capture_requested = threading.Event()
        self.capture_requested.set()
        self.recorder = None
        self.out = queue.Queue()


class ScreenCaptureTests(unittest.TestCase):
    def setUp(self):
        self.app = FakeApp()
        self.worker = pcconnect_app.ScreenCaptureWorker(self.app)
        self.jpeg = b"\xff\xd8test-jpeg\xff\xd9"

    def test_live_view_uses_6802_and_keeps_working_without_breeze(self):
        with patch.object(switchu_remote, "capture", return_value=self.jpeg) as capture:
            self.assertTrue(self.worker.capture_live_once())

        capture.assert_called_once_with("192.0.2.1", "pair", timeout=3.0)
        self.assertFalse(self.app.link.capture_on.is_set())
        self.assertEqual(self.app.out.get_nowait(), ("frame", self.jpeg))

    def test_6802_failure_falls_back_to_breeze_and_caches_retry(self):
        self.app.recorder = FakeRecorder()
        with patch.object(switchu_remote, "capture", side_effect=switchu_remote.RemoteError("offline")) as capture:
            self.assertFalse(self.worker.capture_live_once())
            self.assertTrue(self.app.link.capture_on.is_set())
            first_status = self.app.out.get_nowait()
            self.assertEqual(first_status[0], "status")
            self.assertIn("using Breeze capture", first_status[1])

            self.assertFalse(self.worker.capture_live_once())
            capture.assert_called_once()
        self.assertEqual(self.app.recorder.frames, [])

    def test_screenshot_button_capture_uses_6802_without_breeze_fallback(self):
        with patch.object(switchu_remote, "capture", return_value=self.jpeg) as capture:
            self.assertTrue(self.worker.capture_screenshot_once())

        capture.assert_called_once_with("192.0.2.1", "pair", timeout=3.0)
        self.assertEqual(self.app.link.jobs, [])
        self.assertEqual(self.app.out.get_nowait(), ("screenshot", self.jpeg))

    def test_screenshot_uses_existing_breeze_path_during_switchu_backoff(self):
        self.worker.retry_after = 10**12
        self.assertFalse(self.worker.capture_screenshot_once())
        self.assertEqual(self.app.link.jobs, ["capture"])

    def test_app_screenshot_action_queues_capture_worker_request(self):
        app = object.__new__(pcconnect_app.App)

        class CaptureRequest:
            requested = False

            def request_screenshot(self):
                self.requested = True

        app.screen_capture = CaptureRequest()
        app.on_screenshot()
        self.assertTrue(app.screen_capture.requested)

    def test_offline_nro_frame_cannot_queue_stale_breeze_menu_action(self):
        app = object.__new__(pcconnect_app.App)
        app.canvas = FakeCanvas()
        app.to_screen = lambda x, y: (x, y)
        app.hit = lambda x, y: ("button", 17)
        app.state = {"keyboard": False, "menu": {"buttons": [{"id": 17}]}}
        app.pad = None
        app.link = FakeLink()
        app.status = type("Status", (), {"config": lambda _self, **kw: setattr(_self, "text", kw["text"])})()
        app.breeze_unavailable = pcconnect_app.App.breeze_unavailable.__get__(app)
        event = type("Click", (), {"x": 20, "y": 30})()

        pcconnect_app.App.on_click(app, event)

        self.assertEqual(app.link.jobs, [])
        self.assertEqual(app.status.text, "Breeze is closed; use controller for NRO.")

    def test_connected_breeze_canvas_action_still_uses_existing_command(self):
        app = object.__new__(pcconnect_app.App)
        app.canvas = FakeCanvas()
        app.to_screen = lambda x, y: (x, y)
        app.hit = lambda x, y: ("button", 17)
        app.state = {"keyboard": False}
        app.pad = None
        app.link = FakeLink()
        app.link.connected.set()
        app.status = type("Status", (), {"config": lambda _self, **kw: setattr(_self, "text", kw["text"])})()
        event = type("Click", (), {"x": 20, "y": 30})()

        pcconnect_app.App.on_click(app, event)

        self.assertEqual(app.link.jobs, ["button 17"])


if __name__ == "__main__":
    unittest.main()
