"""Developer helpers used by the host Makefile."""

from __future__ import annotations

import os
import sys
from pathlib import Path

from dotenv import dotenv_values
from serial.tools import list_ports

PORT_HINTS = ("usbmodem", "usbserial", "arduino", "wchusb", "ttyacm", "ttyusb")


def _is_likely_board(port) -> bool:
    name = f"{port.device} {port.description or ''} {port.hwid or ''}".lower()
    return any(hint in name for hint in PORT_HINTS)


def _find_board() -> str | None:
    for port in list_ports.comports():
        if _is_likely_board(port):
            return port.device
    return None


def port() -> int:
    ports = list(list_ports.comports())
    for item in ports:
        name = f"{item.device} {item.description or ''} {item.hwid or ''}"
        mark = "*" if _is_likely_board(item) else " "
        print(f"{mark} {item.device:16} {item.description or ''} {item.hwid or ''}")
    if not ports:
        print("no serial ports found")
    return 0


def set_port() -> int:
    board = _find_board()
    if not board:
        print("no Arduino-like serial port found; plug in the board and run `make port`", file=sys.stderr)
        return 1

    path = Path(".env")
    lines = path.read_text().splitlines() if path.exists() else []
    for idx, line in enumerate(lines):
        if line.startswith("SF4_PORT="):
            lines[idx] = f"SF4_PORT={board}"
            break
    else:
        lines.append(f"SF4_PORT={board}")

    path.write_text("\n".join(lines).rstrip() + "\n")
    print(f"SF4_PORT={board}")
    return 0


def doctor() -> int:
    cfg = dotenv_values(".env") if Path(".env").exists() else {}
    print(f"Host:   http://{os.environ.get('HOST', '127.0.0.1')}:{os.environ.get('PORT', '8000')}")
    print(f"SF4_PORT={os.environ.get('SF4_PORT') or 'auto-detect'}")
    for key in (
        "OPENAI_API_KEY",
        "OPENAI_BASE_URL",
        "OPENAI_CA_BUNDLE",
        "OPENAI_CHAT_MODEL",
        "OPENAI_TRANSCRIBE_MODEL",
    ):
        value = cfg.get(key) or os.environ.get(key) or ""
        print(f"{key}={'set' if value else 'blank'}")
    return 0


def main(argv: list[str] | None = None) -> int:
    argv = argv if argv is not None else sys.argv[1:]
    cmd = argv[0] if argv else ""
    if cmd == "port":
        return port()
    if cmd == "set-port":
        return set_port()
    if cmd == "doctor":
        return doctor()
    print("usage: python -m app.devtools {port|set-port|doctor}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
