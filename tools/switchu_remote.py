#!/usr/bin/env python3
"""Small client for SwitchU's independent remote NRO launch channel.

This connection does not depend on Breeze being open. A successful ``launch``
receipt means the daemon queued the bundled loader; ``started`` means the
loader entered. Neither confirms that the requested NRO initialized.
"""

from __future__ import annotations

import argparse
import getpass
import os
import re
import socket
import sys
import time
import unicodedata

DEFAULT_PORT = 6802
DEFAULT_BREEZE = "/switch/Breeze/Breeze.nro"
_STATE_RE = re.compile(r"(?:^|\s)state=(idle|queued|launching|started|finished|failed)(?:\s|$)")
_ID_RE = re.compile(r"(?:^|\s)id=(\d+)(?:\s|$)")


class RemoteError(Exception):
    """The daemon rejected a request or returned an invalid protocol reply."""


class AmbiguousLaunchError(RemoteError):
    """The launch request was sent, but no receipt arrived; never resend it."""


class AmbiguousInputError(RemoteError):
    """The button press may have arrived, but no receipt arrived; never resend it."""


class AmbiguousControllerError(RemoteError):
    """Controller enable state may have changed, but its receipt was lost."""


INPUT_BUTTONS = frozenset((
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "PLUS", "MINUS",
    "LSTICK", "RSTICK", "UP", "DOWN", "LEFT", "RIGHT", "HOME", "CAPTURE",
    "LSL", "LSR", "RSL", "RSR",
))


def validate_input(button: str, duration_ms: int) -> tuple[str, int]:
    if not isinstance(button, str) or button not in INPUT_BUTTONS:
        raise ValueError("unsupported Switch button")
    if (isinstance(duration_ms, bool) or not isinstance(duration_ms, int)
            or not 1 <= duration_ms <= 1000):
        raise ValueError("button duration must be between 1 and 1000 ms")
    return button, duration_ms


def validate_stick(side: str, x: int, y: int, duration_ms: int) -> tuple[str, int, int, int]:
    if side not in ("L", "R"):
        raise ValueError("stick side must be L or R")
    values = (x, y, duration_ms)
    if any(isinstance(value, bool) or not isinstance(value, int) for value in values):
        raise ValueError("stick axes and duration must be integers")
    if not -32768 <= x <= 32767 or not -32768 <= y <= 32767:
        raise ValueError("stick axes must be between -32768 and 32767")
    if not 1 <= duration_ms <= 1000:
        raise ValueError("stick duration must be between 1 and 1000 ms")
    return side, x, y, duration_ms


def validate_sd_path(path: str) -> str:
    """Validate an absolute SD NRO path accepted by the daemon protocol."""
    if not isinstance(path, str):
        raise ValueError("NRO path must be text")
    has_sdmc_prefix = path[:5].lower() == "sdmc:"
    absolute = path[5:] if has_sdmc_prefix else path
    if (not absolute.startswith("/") or absolute.endswith("/")
            or not absolute.lower().endswith(".nro")
            or any(unicodedata.category(char) == "Cc" for char in path)
            or '"' in path or "\\" in path
            or ":" in (absolute if has_sdmc_prefix else path)
            or any(part in ("", ".", "..") for part in absolute[1:].split("/"))):
        raise ValueError("enter an absolute SD .nro path (for example /hbmenu.nro)")
    canonical = "sdmc:" + absolute
    try:
        path_size = len(canonical.encode("utf-8"))
    except UnicodeEncodeError as exc:
        raise ValueError("NRO path must be valid UTF-8") from exc
    if path_size > 511:
        raise ValueError("SD NRO path is too long (maximum 511 UTF-8 bytes including sdmc:)")
    return path


def _read_line(sock: socket.socket, limit: int = 4096, *, deadline=None) -> str:
    data = bytearray()
    while len(data) < limit:
        if deadline is not None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("SwitchU reply deadline expired")
            sock.settimeout(remaining)
        chunk = sock.recv(1)
        if not chunk:
            raise ConnectionError("SwitchU closed the connection")
        if chunk == b"\n":
            try:
                return data.decode("utf-8").rstrip("\r")
            except UnicodeDecodeError as exc:
                raise RemoteError("SwitchU returned invalid text") from exc
        data.extend(chunk)
    raise RemoteError("SwitchU reply is too long")


def request(host: str, code: str, command: str, *, port: int = DEFAULT_PORT,
            timeout: float = 3.0) -> str:
    """Send one authenticated command. Launches and inputs are never replayed."""
    if not code or "\n" in code or "\r" in code:
        raise ValueError("PC connect code is empty or invalid")
    if (command not in ("status", "controller status", "controller on", "controller off")
            and not command.startswith(("launch ", "press ", "stick "))):
        raise ValueError("unsupported SwitchU command")
    path = None
    input_press = None
    input_stick = None
    controller_action = command if command in ("controller on", "controller off") else None
    if command.startswith("launch "):
        path = validate_sd_path(command[len("launch "):])
        command = "launch " + path
    elif command.startswith("press "):
        fields = command.split()
        if len(fields) != 3:
            raise ValueError("press requires a button and duration")
        input_press = validate_input(fields[1], int(fields[2]) if fields[2].isdigit() else fields[2])
        command = "press %s %d" % input_press
    elif command.startswith("stick "):
        fields = command.split()
        if len(fields) != 5:
            raise ValueError("stick requires a side, x, y, and duration")
        parsed = [int(value) if re.fullmatch(r"-?\d+", value) else value for value in fields[2:]]
        input_stick = validate_stick(fields[1], *parsed)
        command = "stick %s %d %d %d" % input_stick

    sent_launch = False
    sent_input = False
    sent_controller = False
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            greeting = _read_line(sock)
            if greeting != "SWITCHU 1":
                raise RemoteError("SwitchU remote channel is unavailable or incompatible")
            sock.sendall(("auth " + code + "\n").encode("utf-8"))
            auth = _read_line(sock)
            if auth != "OK auth":
                if auth.startswith("ERR "):
                    raise RemoteError(auth[4:])
                raise RemoteError("SwitchU authentication failed")
            sent_launch = path is not None
            sent_input = input_press is not None or input_stick is not None
            sent_controller = controller_action is not None
            sock.sendall((command + "\n").encode("utf-8"))
            reply = _read_line(sock)
    except (OSError, ConnectionError, TimeoutError) as exc:
        if sent_controller:
            raise AmbiguousControllerError(
                "SwitchU controller state is unknown; check status before acting again"
            ) from exc
        if sent_input:
            raise AmbiguousInputError(
                "button press was sent but its receipt was not received; do not repeat it automatically"
            ) from exc
        if sent_launch:
            raise AmbiguousLaunchError(
                "launch was sent but its receipt was not received; check status before retrying"
            ) from exc
        raise RemoteError("cannot reach SwitchU remote channel: %s" % exc) from exc
    except RemoteError as exc:
        if sent_controller:
            raise AmbiguousControllerError(
                "SwitchU controller state is unknown; check status before acting again"
            ) from exc
        if sent_input:
            raise AmbiguousInputError(
                "button press was sent but its receipt was invalid; do not repeat it automatically"
            ) from exc
        raise

    if reply.startswith("ERR "):
        raise RemoteError(reply[4:])
    expected = ("OK pressed %s ms=%d" % input_press if input_press is not None
                else "OK stick %s x=%d y=%d ms=%d" % input_stick if input_stick is not None
                else "OK controller enabled" if command == "controller on"
                else "OK controller disabled" if command == "controller off"
                else "OK controller " if command == "controller status"
                else "OK status " if path is None else "OK queued ")
    if not reply.startswith(expected):
        if sent_input:
            raise AmbiguousInputError(
                "SwitchU returned no valid button receipt; the press may have been accepted"
            )
        if controller_action is not None:
            raise AmbiguousControllerError(
                "SwitchU controller state is unknown; check status before acting again"
            )
        if command == "controller status":
            raise RemoteError("unexpected SwitchU controller status reply")
        if path is not None:
            raise AmbiguousLaunchError(
                "launch was sent but SwitchU returned no valid receipt; check status before retrying"
            )
        raise RemoteError("unexpected SwitchU status reply")
    if controller_action is not None and reply != expected:
        raise AmbiguousControllerError(
            "SwitchU returned an invalid controller receipt; controller state is unknown"
        )
    if sent_input and reply != expected:
        raise AmbiguousInputError(
            "SwitchU returned no valid button receipt; the press may have been accepted"
        )
    if command == "controller status" and reply not in ("OK controller enabled", "OK controller disabled"):
        raise RemoteError("unexpected SwitchU controller status reply")
    if path is not None and re.fullmatch(r"OK queued [1-9]\d*", reply) is None:
        raise AmbiguousLaunchError("launch was sent but its receipt ID is invalid; check status before retrying")
    return reply


def status(host: str, code: str, *, port: int = DEFAULT_PORT, timeout: float = 3.0) -> str:
    return request(host, code, "status", port=port, timeout=timeout)


def launch(host: str, code: str, path: str = DEFAULT_BREEZE, *,
           port: int = DEFAULT_PORT, timeout: float = 3.0) -> str:
    return request(host, code, "launch " + validate_sd_path(path), port=port, timeout=timeout)


def press_button(host: str, code: str, button: str, duration_ms: int = 100, *,
                 port: int = DEFAULT_PORT, timeout: float = 3.0) -> str:
    button, duration_ms = validate_input(button, duration_ms)
    return request(host, code, "press %s %d" % (button, duration_ms),
                   port=port, timeout=timeout)


def stick(host: str, code: str, side: str, x: int, y: int, duration_ms: int = 100, *,
          port: int = DEFAULT_PORT, timeout: float = 3.0) -> str:
    side, x, y, duration_ms = validate_stick(side, x, y, duration_ms)
    return request(host, code, "stick %s %d %d %d" % (side, x, y, duration_ms),
                   port=port, timeout=timeout)


def controller(host: str, code: str, enabled: bool | None = None, *,
               port: int = DEFAULT_PORT, timeout: float = 3.0) -> str:
    if enabled is None:
        command = "controller status"
    elif isinstance(enabled, bool):
        command = "controller on" if enabled else "controller off"
    else:
        raise ValueError("controller enabled must be True, False, or None for status")
    return request(host, code, command, port=port, timeout=timeout)


def capture(host: str, code: str, *, port: int = DEFAULT_PORT,
            timeout: float = 3.0, layer_stack: int | None = None) -> bytes:
    """Read the console's current 1280x720 JPEG without opening Breeze."""
    if not code or "\n" in code or "\r" in code:
        raise ValueError("PC connect code is empty or invalid")
    if layer_stack is not None and (type(layer_stack) is not int or layer_stack not in (0, 1, 2, 3, 4, 6)):
        raise ValueError("unsupported capture layer stack")
    deadline = time.monotonic() + timeout
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            if _read_line(sock, deadline=deadline) != "SWITCHU 1":
                raise RemoteError("SwitchU remote channel is unavailable or incompatible")
            sock.sendall(("auth " + code + "\n").encode("utf-8"))
            if _read_line(sock, deadline=deadline) != "OK auth":
                raise RemoteError("SwitchU authentication failed")
            command = "capture" if layer_stack is None else "capture %d" % layer_stack
            sock.sendall((command + "\n").encode("ascii"))
            header = _read_line(sock, deadline=deadline)
            if header.startswith("ERR "):
                raise RemoteError(header[4:])
            match = re.fullmatch(r"OK capture bytes=([1-9]\d*) width=1280 height=720 format=jpeg", header)
            if not match:
                raise RemoteError("invalid SwitchU capture header")
            size = int(match[1])
            if not 4 <= size <= 0x80000:
                raise RemoteError("SwitchU capture size is out of bounds")
            data = bytearray()
            while len(data) < size:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RemoteError("SwitchU capture payload timed out")
                sock.settimeout(remaining)
                chunk = sock.recv(min(65536, size - len(data)))
                if not chunk:
                    raise RemoteError("SwitchU capture payload is incomplete")
                data.extend(chunk)
            if data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
                raise RemoteError("SwitchU capture is not a complete JPEG")
            return bytes(data)
    except (OSError, ConnectionError, TimeoutError) as exc:
        raise RemoteError("cannot capture SwitchU screen: %s" % exc) from exc


def request_id(reply: str) -> int | None:
    queued = re.fullmatch(r"OK queued ([1-9]\d*)", reply)
    if queued:
        return int(queued.group(1))
    match = _ID_RE.search(reply)
    return int(match.group(1)) if match else None


def status_state(reply: str) -> str | None:
    match = _STATE_RE.search(reply)
    return match.group(1) if match else None


def wait_for_request(host: str, code: str, request_number: int, *, port: int = DEFAULT_PORT,
                     timeout: float = 3.0, deadline: float = 20.0,
                     interval: float = 0.5) -> str:
    """Poll status only, never replaying the launch command."""
    end = time.monotonic() + max(0.0, deadline)
    last = ""
    while True:
        last = status(host, code, port=port, timeout=timeout)
        current = request_id(last)
        state = status_state(last)
        if current == request_number and state in ("started", "finished", "failed"):
            return last
        if current is not None and current > request_number:
            return last
        if time.monotonic() >= end:
            return last
        time.sleep(min(interval, max(0.0, end - time.monotonic())))


def _credentials(args) -> str:
    code = os.environ.get(args.code_env) if args.code_env else None
    if code is None:
        code = getpass.getpass("Breeze PC connect code: ")
    if not code:
        raise ValueError("PC connect code is empty")
    return code


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Send a one-shot SwitchU remote launch request")
    parser.add_argument("host", help="Switch IP address")
    parser.add_argument("command", choices=("status", "launch"))
    parser.add_argument("path", nargs="?", help="absolute SD path, e.g. '/hbmenu.nro' or '/switch/Breeze/Breeze.nro'")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--code-env", metavar="VARIABLE",
                        help="read the Breeze PC pairing code from this environment variable")
    parser.add_argument("--wait", type=float, default=0.0, metavar="SECONDS",
                        help="after queue receipt, poll status for up to this many seconds")
    args = parser.parse_args(argv)
    try:
        code = _credentials(args)
        if args.command == "status":
            if args.path is not None:
                parser.error("status does not take a path")
            print(status(args.host, code, port=args.port))
            return 0
        receipt = launch(args.host, code, args.path or DEFAULT_BREEZE, port=args.port)
        print(receipt)
        number = request_id(receipt)
        if args.wait > 0 and number is not None:
            observed = wait_for_request(args.host, code, number, port=args.port,
                                        deadline=min(args.wait, 300.0))
            print(observed)
            newer = request_id(observed)
            state = status_state(observed)
            if newer is not None and newer > number:
                print("Request %d was superseded by newer request %d; it was not resent." % (number, newer))
                return 3
            elif state in ("finished", "failed"):
                print("Loader request %s; requested NRO initialization is not confirmed." % state)
                if state == "failed":
                    return 2
        return 0
    except (RemoteError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
