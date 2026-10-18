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


def _read_line(sock: socket.socket, limit: int = 4096) -> str:
    data = bytearray()
    while len(data) < limit:
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
    """Send one authenticated command. A launch is sent at most once."""
    if not code or "\n" in code or "\r" in code:
        raise ValueError("PC connect code is empty or invalid")
    if command != "status" and not command.startswith("launch "):
        raise ValueError("unsupported SwitchU command")
    path = None
    if command.startswith("launch "):
        path = validate_sd_path(command[len("launch "):])
        command = "launch " + path

    sent_launch = False
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
            sock.sendall((command + "\n").encode("utf-8"))
            reply = _read_line(sock)
    except (OSError, ConnectionError, TimeoutError) as exc:
        if sent_launch:
            raise AmbiguousLaunchError(
                "launch was sent but its receipt was not received; check status before retrying"
            ) from exc
        raise RemoteError("cannot reach SwitchU remote channel: %s" % exc) from exc

    if reply.startswith("ERR "):
        raise RemoteError(reply[4:])
    expected = "OK status " if path is None else "OK queued "
    if not reply.startswith(expected):
        if path is not None:
            raise AmbiguousLaunchError(
                "launch was sent but SwitchU returned no valid receipt; check status before retrying"
            )
        raise RemoteError("unexpected SwitchU status reply")
    if path is not None and re.fullmatch(r"OK queued [1-9]\d*", reply) is None:
        raise AmbiguousLaunchError("launch was sent but its receipt ID is invalid; check status before retrying")
    return reply


def status(host: str, code: str, *, port: int = DEFAULT_PORT, timeout: float = 3.0) -> str:
    return request(host, code, "status", port=port, timeout=timeout)


def launch(host: str, code: str, path: str = DEFAULT_BREEZE, *,
           port: int = DEFAULT_PORT, timeout: float = 3.0) -> str:
    return request(host, code, "launch " + validate_sd_path(path), port=port, timeout=timeout)


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
