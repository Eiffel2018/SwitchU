import socket
import os
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from tools import switchu_remote as remote


class MockSwitchU:
    def __init__(self, *, auth=True, command_reply="OK queued 7", greeting="SWITCHU 1",
                 close_after_command=False):
        self.auth = auth
        self.command_reply = command_reply
        self.greeting = greeting
        self.close_after_command = close_after_command
        self.commands = []
        self.auth_lines = []
        self.server = socket.socket()
        self.server.bind(("127.0.0.1", 0))
        self.server.listen()
        self.server.settimeout(0.2)
        self.port = self.server.getsockname()[1]
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        while not self.stop.is_set():
            try:
                conn, _ = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with conn:
                conn.settimeout(2)
                conn.sendall((self.greeting + "\n").encode())
                reader = conn.makefile("rb")
                auth = reader.readline().decode().rstrip("\r\n")
                self.auth_lines.append(auth)
                if not self.auth or not auth.startswith("auth "):
                    conn.sendall(b"ERR auth\n")
                    continue
                conn.sendall(b"OK auth\n")
                line = reader.readline().decode().rstrip("\r\n")
                self.commands.append(line)
                if self.close_after_command:
                    continue
                conn.sendall((self.command_reply + "\n").encode())

    def close(self):
        self.stop.set()
        self.server.close()
        self.thread.join(1)


class SwitchURemoteTests(unittest.TestCase):
    def with_server(self, **kwargs):
        server = MockSwitchU(**kwargs)
        self.addCleanup(server.close)
        return server

    def test_auth_status_framing_and_status_fields(self):
        server = self.with_server(command_reply="OK status id=7 state=started rc=0x0")
        reply = remote.status("127.0.0.1", "1234", port=server.port)
        self.assertEqual(server.auth_lines, ["auth 1234"])
        self.assertEqual(server.commands, ["status"])
        self.assertEqual(remote.request_id(reply), 7)
        self.assertEqual(remote.status_state(reply), "started")

    def test_launch_path_with_spaces_is_one_queued_command(self):
        server = self.with_server()
        reply = remote.launch("127.0.0.1", "pair", "/switch/My Homebrew/Breeze.nro", port=server.port)
        self.assertEqual(reply, "OK queued 7")
        self.assertEqual(remote.request_id(reply), 7)
        self.assertEqual(server.commands, ["launch /switch/My Homebrew/Breeze.nro"])

    def test_sdmc_prefixed_paths_are_preserved(self):
        server = self.with_server()
        remote.launch("127.0.0.1", "pair", "sdmc:/switch/Tools/My Tool.nro", port=server.port)
        self.assertEqual(server.commands, ["launch sdmc:/switch/Tools/My Tool.nro"])

    def test_absolute_root_path_and_case_insensitive_extension(self):
        server = self.with_server()
        remote.launch("127.0.0.1", "pair", "/hbmenu.NRO", port=server.port)
        self.assertEqual(server.commands, ["launch /hbmenu.NRO"])

    def test_path_byte_limit_includes_sdmc_prefix(self):
        max_path = "/" + ("a" * 501) + ".nro"
        self.assertEqual(len(("sdmc:" + max_path).encode("utf-8")), 511)
        self.assertEqual(remote.validate_sd_path(max_path), max_path)
        with self.assertRaisesRegex(ValueError, "511 UTF-8 bytes"):
            remote.validate_sd_path("/" + ("a" * 502) + ".nro")
        with self.assertRaisesRegex(ValueError, "511 UTF-8 bytes"):
            remote.validate_sd_path("/" + ("é" * 251) + ".nro")

    def test_invalid_path_is_rejected_before_network(self):
        server = self.with_server()
        for path in (
            "C:/Breeze.nro", "/switch/../Breeze.nro", "/switch/./Tool.nro",
            "/switch//Tool.nro", "/switch/a.txt", "/switch/a\nstatus",
            "/switch/Tool:Alt.nro", '/switch/Quoted"Name.nro', "/switch/Tool\\Name.nro",
            "/switch/Control\x01Name.nro", "/switch/Control\x85Name.nro",
        ):
            with self.subTest(path=path), self.assertRaises(ValueError):
                remote.launch("127.0.0.1", "pair", path, port=server.port)
        self.assertEqual(server.commands, [])

    def test_busy_and_auth_errors_are_reported(self):
        server = self.with_server(command_reply="ERR busy")
        with self.assertRaisesRegex(remote.RemoteError, "busy"):
            remote.launch("127.0.0.1", "pair", port=server.port)
        denied = self.with_server(auth=False)
        with self.assertRaisesRegex(remote.RemoteError, "auth"):
            remote.status("127.0.0.1", "wrong", port=denied.port)
        self.assertEqual(denied.commands, [])

    def test_ambiguous_launch_is_not_retried(self):
        server = self.with_server(close_after_command=True)
        with self.assertRaises(remote.AmbiguousLaunchError):
            remote.launch("127.0.0.1", "pair", port=server.port, timeout=0.5)
        self.assertEqual(server.commands, ["launch /switch/Breeze/Breeze.nro"])

    def test_bad_greeting_does_not_send_credentials_or_command(self):
        server = self.with_server(greeting="HELLO")
        with self.assertRaisesRegex(remote.RemoteError, "incompatible"):
            remote.status("127.0.0.1", "secret", port=server.port)
        self.assertFalse(any(server.auth_lines))
        self.assertEqual(server.commands, [])

    def test_press_button_sends_one_supported_input_and_validates_receipt(self):
        server = self.with_server(command_reply="OK pressed PLUS ms=100")
        reply = remote.press_button("127.0.0.1", "pair", "PLUS", 100, port=server.port)
        self.assertEqual(reply, "OK pressed PLUS ms=100")
        self.assertEqual(server.commands, ["press PLUS 100"])

    def test_invalid_input_button_or_duration_is_rejected_before_network(self):
        server = self.with_server()
        for button, duration in (("HOME", 100), ("plus", 100), ("A\nstatus", 100),
                                 ("A", 0), ("A", 1001), ("A", True), ("A", "100")):
            with self.subTest(button=button, duration=duration), self.assertRaises(ValueError):
                remote.press_button("127.0.0.1", "pair", button, duration, port=server.port)
        self.assertEqual(server.commands, [])

    def test_ambiguous_input_is_not_retried(self):
        server = self.with_server(close_after_command=True)
        with self.assertRaises(remote.AmbiguousInputError):
            remote.press_button("127.0.0.1", "pair", "PLUS", 100,
                                port=server.port, timeout=0.5)
        self.assertEqual(server.commands, ["press PLUS 100"])

    def test_invalid_input_success_receipt_is_ambiguous_without_retry(self):
        server = self.with_server(command_reply="OK pressed PLUS ms=99")
        with self.assertRaises(remote.AmbiguousInputError):
            remote.press_button("127.0.0.1", "pair", "PLUS", 100, port=server.port)
        self.assertEqual(server.commands, ["press PLUS 100"])

    def test_input_error_receipt_is_reported_as_rejection(self):
        server = self.with_server(command_reply="ERR input rc=0x1234")
        with self.assertRaisesRegex(remote.RemoteError, "input rc=0x1234"):
            remote.press_button("127.0.0.1", "pair", "A", 100, port=server.port)
        self.assertEqual(server.commands, ["press A 100"])

    def test_wait_returns_immediately_when_newer_request_supersedes(self):
        server = self.with_server(command_reply="OK status id=8 state=queued rc=0x00000000")
        started = time.monotonic()
        reply = remote.wait_for_request("127.0.0.1", "pair", 7, port=server.port,
                                        deadline=20, interval=0.5)
        self.assertLess(time.monotonic() - started, 1.0)
        self.assertEqual(remote.request_id(reply), 8)
        self.assertEqual(server.commands, ["status"])


if __name__ == "__main__":
    unittest.main()
