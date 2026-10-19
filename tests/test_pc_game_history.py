import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS_DIR))
from pc_game_history import clean, remember, valid_tid  # noqa: E402

APP_DIR = REPO_ROOT / "tools" / "breezepc"
pcconnect_app = None
if APP_DIR.is_dir():
    sys.path.insert(0, str(APP_DIR))
    try:
        import pcconnect_app  # noqa: E402
    except ImportError:
        pcconnect_app = None


class FakeButton:
    def winfo_rootx(self):
        return 10

    def winfo_rooty(self):
        return 20

    def winfo_height(self):
        return 5


class FakeRoot:
    def after_idle(self, callback):
        callback()


class FakeLink:
    def __init__(self):
        self.sent = []

    def send(self, command):
        self.sent.append(command)


class FakeLabel:
    def __init__(self):
        self.text = ""

    def config(self, **kwargs):
        self.text = kwargs.get("text", self.text)


class FakeMenu:
    instances = []

    def __init__(self, root, tearoff):
        self.items = []
        self.bindings = {}
        self.live = True
        self.__class__.instances.append(self)

    def bind(self, sequence, callback, add=None):
        self.bindings[sequence] = callback

    def add_command(self, **kwargs):
        self.items.append(kwargs)

    def add_separator(self):
        self.items.append({"separator": True})

    def tk_popup(self, x, y):
        assert (x, y) == (10, 25)

    def grab_release(self):
        pass

    def unpost(self):
        callback = self.bindings.get("<Unmap>")
        if callback:
            callback(None)

    def winfo_exists(self):
        return self.live

    def destroy(self):
        self.live = False


class GameHistoryTests(unittest.TestCase):
    def test_tid_validation_and_recent_deduplication(self):
        self.assertEqual(valid_tid("010000000000100a"), "010000000000100A")
        self.assertEqual(valid_tid("0000000000000000"), "")
        self.assertEqual(valid_tid("010000000000100"), "")
        self.assertEqual(valid_tid("010000000000100Z"), "")
        prior = [
            {"tid": "0100000000001000", "name": "Alpha"},
            {"tid": "bad", "name": "Discard"},
            {"tid": "0100000000002000", "name": "Beta"},
            {"tid": "0100000000002000", "name": "Duplicate"},
        ]
        result = remember(prior, "0100000000002000", "Beta updated")
        self.assertEqual(result, [
            {"tid": "0100000000002000", "name": "Beta updated"},
            {"tid": "0100000000001000", "name": "Alpha"},
        ])
        self.assertEqual(remember(result, "0100000000002000", "")[0]["name"], "Beta updated")
        self.assertEqual(prior[0]["tid"], "0100000000001000")
        self.assertEqual(clean(result), result)
        self.assertEqual(len(remember([], "0100000000001000", "Alpha", limit=1)), 1)

    def make_app(self):
        app = pcconnect_app.App.__new__(pcconnect_app.App)
        app.root = FakeRoot()
        app.start_btn = FakeButton()
        app.cfg = {
            "game_history": [
                {"tid": "0100000000001000", "name": "Alpha"},
                {"tid": "0100000000002000", "name": "Beta"},
            ],
            "last_game": ["0100000000003000", "Legacy"],
        }
        app.save_tid = ""
        app.save = {"game_running": False}
        app.link = FakeLink()
        app.send_switchu_game = Mock(side_effect=lambda tid: app.link.send("gamestart " + tid))
        app.status = FakeLabel()
        app.save_cfg = Mock()
        return app

    @unittest.skipIf(pcconnect_app is None, "BreezePC source is only available on the configured PC")
    def test_start_menu_order_selection_and_restore_status(self):
        app = self.make_app()
        FakeMenu.instances.clear()
        with patch.object(pcconnect_app.tk, "Menu", FakeMenu):
            app.on_start_game()
            menu = FakeMenu.instances[-1]
            self.assertEqual([item["label"] for item in menu.items if "label" in item], [
                "Enter TID...",
                "Alpha  [0100000000001000]",
                "Beta  [0100000000002000]",
                "Legacy  [0100000000003000]",
            ])
            menu.items[3]["command"]()
            self.assertEqual(app.link.sent, ["gamestart 0100000000002000"])
            app.on_game_reply("gamestart 0100000000002000", "+OK starting")
            self.assertIn("Waiting for the daemon to restore Breeze", app.status.text)
            self.assertNotIn("press HOME", app.status.text)

    @unittest.skipIf(pcconnect_app is None, "BreezePC source is only available on the configured PC")
    def test_cancel_sends_nothing_and_reopen_replaces_popup(self):
        app = self.make_app()
        FakeMenu.instances.clear()
        with patch.object(pcconnect_app.tk, "Menu", FakeMenu):
            app.on_start_game()
            first = FakeMenu.instances[-1]
            first.unpost()  # Escape/click-away path: closes without selecting a command.
            self.assertEqual(app.link.sent, [])
            self.assertIsNone(app.start_menu)
            self.assertFalse(first.live)

            app.on_start_game()
            first = FakeMenu.instances[-1]
            app.on_start_game()
            second = FakeMenu.instances[-1]
            self.assertIsNot(first, second)
            self.assertFalse(first.live)
            self.assertTrue(second.live)
            self.assertEqual(app.link.sent, [])

    @unittest.skipIf(pcconnect_app is None, "BreezePC source is only available on the configured PC")
    def test_running_game_blocks_open_and_start(self):
        app = self.make_app()
        app.save = {"game_running": True}
        FakeMenu.instances.clear()
        with patch.object(pcconnect_app.tk, "Menu", FakeMenu):
            app.on_start_game()
            self.assertEqual(FakeMenu.instances, [])
            app.start_game("0100000000001000")
            with patch.object(pcconnect_app.simpledialog, "askstring") as ask:
                app.on_start_game_tid()
                ask.assert_not_called()
        self.assertEqual(app.link.sent, [])

    def test_manual_start_available_without_history_and_remembered_on_acceptance(self):
        app = self.make_app()
        app.cfg = {}
        with patch.object(pcconnect_app.tk, "Menu", FakeMenu), \
                patch.object(pcconnect_app.simpledialog, "askstring", return_value=" 0x0100005027a18000 "):
            app.on_start_game()
            menu = FakeMenu.instances[-1]
            self.assertEqual(len(menu.items), 1)
            menu.items[0]["command"]()
        self.assertEqual(app.link.sent, ["gamestart 0100005027A18000"])
        self.assertNotIn("game_history", app.cfg)
        app.on_game_reply(app.link.sent[0], "+OK starting")
        self.assertEqual(app.cfg["game_history"], [{"tid": "0100005027A18000", "name": ""}])
        app.save_cfg.assert_called_once()

    def test_manual_invalid_inputs_then_cancel_send_nothing(self):
        app = self.make_app()
        with patch.object(pcconnect_app.simpledialog, "askstring",
                          side_effect=["0000000000000000", "0100005027A1800Z", None]), \
                patch.object(pcconnect_app.messagebox, "showerror") as error:
            app.on_start_game_tid()
        self.assertEqual(error.call_count, 2)
        self.assertEqual(app.link.sent, [])
        app.save_cfg.assert_not_called()

    def test_rejected_start_does_not_add_history(self):
        app = self.make_app()
        before = list(app.cfg["game_history"])
        with patch.object(pcconnect_app.messagebox, "showerror"):
            app.on_game_reply("gamestart 0100005027A18000", "-ERR not installed")
        self.assertEqual(app.cfg["game_history"], before)
        app.save_cfg.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
