#!/usr/bin/env python3
"""
SF4 host-side serial translation layer.

Bridges the host (LLM-produced JSON DSP parameters) and the Arduino DSP pipeline:
  - serialises *validated* JSON parameters into the MCU's compact "S" frame
  - streams it over USB serial
  - parses the MCU's "T" status/telemetry frames back into dicts

Wire protocol (matches DSP_Pipeline.ino):
  PC -> MCU : S,<clip>,<b0>,<b1>,<b2>,<a1>,<a2>,<delay>,<fb>,<mix>,<gain>\\n
  MCU -> PC : T,<clipFlag>,<gain>,<peak>,<rxErrors>\\n   (~10 Hz)

JSON input schema (all keys optional; missing keys keep clean defaults):
  {
    "clip_threshold": 512,
    "biquad": {"b0": 8192, "b1": 0, "b2": 0, "a1": 0, "a2": 0},
    "delay_samples": 300,
    "feedback": 128,
    "mix": 200,
    "gain": 180
  }

Usage:
  pip install pyserial
  python sf4_serial.py --port /dev/tty.usbmodemXXXX --json preset.json
  python sf4_serial.py --port /dev/tty.usbmodemXXXX --demo lowpass
  python sf4_serial.py            # auto-detect port, send a clean passthrough
"""

import argparse
import json
import math
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial not installed.  Run:  pip install pyserial")

BAUD = 115200
FS = 19230.0          # must match the firmware's effective sample rate
Q_ONE = 1 << 13       # Q13 fixed-point 1.0 == 8192

# Clean-passthrough defaults (mirror the firmware's default Params).
DEFAULTS = {
    "clip_threshold": 512,
    "biquad": {"b0": Q_ONE, "b1": 0, "b2": 0, "a1": 0, "a2": 0},
    "delay_samples": 0,
    "feedback": 0,
    "mix": 0,
    "gain": 0,
}


def _clamp(v, lo, hi):
    return max(lo, min(hi, int(round(v))))


def rbj_lowpass(fc, q=0.707):
    """RBJ-cookbook low-pass -> Q13 ints (same math as the firmware's setBiquad)."""
    w0 = 2 * math.pi * fc / FS
    cosw, alpha = math.cos(w0), math.sin(w0) / (2 * q)
    a0 = 1 + alpha
    b0 = (1 - cosw) / 2 / a0
    b1 = (1 - cosw) / a0
    b2 = b0
    a1 = (-2 * cosw) / a0
    a2 = (1 - alpha) / a0
    return {k: _clamp(v * Q_ONE, -32768, 32767)
            for k, v in zip("b0 b1 b2 a1 a2".split(), (b0, b1, b2, a1, a2))}


def json_to_frame(params: dict) -> str:
    """Validate + clamp JSON params, return the MCU 'S' frame string (no newline)."""
    p = {**DEFAULTS, **params}
    bq = {**DEFAULTS["biquad"], **params.get("biquad", {})}

    clip  = _clamp(p["clip_threshold"], 1, 512)
    b = [_clamp(bq[k], -32768, 32767) for k in ("b0", "b1", "b2", "a1", "a2")]
    delay = _clamp(p["delay_samples"], 0, 512)
    fb    = _clamp(p["feedback"], 0, 255)
    mix   = _clamp(p["mix"], 0, 255)
    gain  = _clamp(p["gain"], 0, 255)

    fields = [clip, *b, delay, fb, mix, gain]
    return "S," + ",".join(str(v) for v in fields)


def parse_telemetry(line: str):
    """Parse a 'T,clip,gain,peak,rxErr' frame into a dict, or None."""
    if not line.startswith("T,"):
        return None
    try:
        _, clip, gain, peak, rx = line.split(",")
        return {"clip": int(clip), "gain": int(gain),
                "peak": int(peak), "rx_errors": int(rx)}
    except ValueError:
        return None


def autodetect_port():
    ports = list(list_ports.comports())
    for p in ports:
        name = (p.device or "") + " " + (p.description or "")
        if any(t in name.lower() for t in ("usbmodem", "usbserial", "arduino", "wchusb", "ttyacm", "ttyusb")):
            return p.device
    return ports[0].device if ports else None


DEMOS = {
    "clean":    {},
    "overdrive": {"clip_threshold": 100, "gain": 200},
    "lowpass":  {"biquad": rbj_lowpass(3000)},
    "slapback": {"delay_samples": 400, "feedback": 150, "mix": 180},
    "fullstack": {"clip_threshold": 90, "gain": 200,
                  "biquad": rbj_lowpass(3000),
                  "delay_samples": 250, "feedback": 120, "mix": 150},
}


def main():
    ap = argparse.ArgumentParser(description="SF4 serial translation layer / test tool")
    ap.add_argument("--port", help="serial port (auto-detect if omitted)")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--json", help="path to a JSON params file")
    ap.add_argument("--demo", choices=sorted(DEMOS), help="send a built-in preset")
    ap.add_argument("--seconds", type=float, default=10.0,
                    help="how long to read telemetry after sending (0 = forever)")
    args = ap.parse_args()

    port = args.port or autodetect_port()
    if not port:
        sys.exit("No serial port found; pass --port explicitly.")

    if args.json:
        with open(args.json) as f:
            params = json.load(f)
    elif args.demo:
        params = DEMOS[args.demo]
    else:
        params = {}                       # clean passthrough

    frame = json_to_frame(params)
    print(f"[host] port   : {port} @ {args.baud}")
    print(f"[host] params : {json.dumps(params)}")
    print(f"[host] -> {frame}")

    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        time.sleep(2.0)                   # let the Uno auto-reset settle
        ser.reset_input_buffer()
        ser.write((frame + "\n").encode())

        t0 = time.time()
        while args.seconds == 0 or time.time() - t0 < args.seconds:
            raw = ser.readline().decode(errors="replace").strip()
            if not raw:
                continue
            tel = parse_telemetry(raw)
            if tel:
                bar = "#" * (tel["peak"] * 30 // 512)        # crude VU meter
                clip = "CLIP" if tel["clip"] else "    "
                print(f"  T  gain={tel['gain']:3d}  peak={tel['peak']:3d} "
                      f"|{bar:<30}| {clip}  rxErr={tel['rx_errors']}")
            else:
                print(f"  . {raw}")                            # echoes / banner


if __name__ == "__main__":
    main()
