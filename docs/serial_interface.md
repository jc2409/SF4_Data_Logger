# Python Serial Interface

## Overview

Python communicates with the Arduino over USB serial at 115200 baud using `pyserial`. A dedicated background thread reads incoming data continuously so the main GUI thread never blocks. A separate method sends command strings to the Arduino.

---

## Installation

```bash
pip install pyserial
```

**Finding the Arduino port:**
- Windows: Device Manager → Ports (COM & LPT) → Arduino Uno (COMx)
- Linux: `ls /dev/ttyACM*` or `ls /dev/ttyUSB*`
- Mac: `ls /dev/cu.usbmodem*`

---

## Message Protocol

### Incoming — Arduino → Python

| Prefix | Format | Example |
|---|---|---|
| `DATA` | `DATA,<br_hz>,<power>,<reg>,<ie>,<apnea>` | `DATA,0.47,1842.3,0.73,0.61,0` |
| `DATA` (with extension) | `DATA,...,<spo2>,<hr>` | `DATA,0.47,1842.3,0.73,0.61,0,98.2,72` |
| `DATA:APNEA` | `DATA:APNEA:<0 or 1>` | `DATA:APNEA:1` |
| `ERR` | `ERR:<message>` | `ERR:MAX30100 not found` |

All messages are ASCII, newline-terminated (`\n`).

### Outgoing — Python → Arduino

| Command | Format | Example |
|---|---|---|
| Severity | `CMD:SEV:<0-3>` | `CMD:SEV:2` |
| Biofeedback rate | `CMD:BF:<bpm>` | `CMD:BF:6` |
| LCD row 1 | `CMD:LCD1:<up to 16 chars>` | `CMD:LCD1:BR 14.2 NORMAL` |
| LCD row 2 | `CMD:LCD2:<up to 16 chars>` | `CMD:LCD2:SpO2 98  HR 72` |
| Alert | `CMD:ALERT:<0 or 1>` | `CMD:ALERT:1` |

---

## SerialHandler Class

```python
# python/serial_handler.py

import serial
import serial.tools.list_ports
import threading
import queue
import time

class SerialHandler:
    """
    Bidirectional serial link to Arduino.
    Background thread reads lines and places them in a thread-safe queue.
    Main thread calls get_message() to retrieve them non-blocking.
    """

    def __init__(self, port: str, baud: int = 115200):
        self.port     = port
        self.baud     = baud
        self.ser      = None
        self._queue   = queue.Queue(maxsize=200)
        self._running = False
        self._thread  = None

    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(
                port     = self.port,
                baudrate = self.baud,
                timeout  = 1.0,
            )
            time.sleep(2.0)              # wait for Arduino reset after DTR assertion
            self.ser.reset_input_buffer()
            self._running = True
            self._thread  = threading.Thread(target=self._read_loop, daemon=True)
            self._thread.start()
            return True
        except serial.SerialException as e:
            print(f"[Serial] Connection failed on {self.port}: {e}")
            return False

    def _read_loop(self):
        while self._running:
            try:
                if self.ser.in_waiting:
                    raw  = self.ser.readline()
                    line = raw.decode('utf-8', errors='ignore').strip()
                    if line and not self._queue.full():
                        self._queue.put(line)
            except serial.SerialException:
                break
            except Exception as e:
                print(f"[Serial] Read error: {e}")

    def get_message(self, timeout: float = 0.0) -> str | None:
        """Return next message or None if queue is empty."""
        try:
            return self._queue.get(block=timeout > 0, timeout=timeout)
        except queue.Empty:
            return None

    def send(self, command: str):
        """Send a command string to the Arduino (newline appended)."""
        if self.ser and self.ser.is_open:
            try:
                self.ser.write((command + '\n').encode('utf-8'))
            except serial.SerialException as e:
                print(f"[Serial] Write error: {e}")

    def disconnect(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
        if self.ser and self.ser.is_open:
            self.ser.close()


def find_arduino_port() -> str | None:
    """Scan all serial ports and return the first that looks like an Arduino."""
    for port in serial.tools.list_ports.comports():
        desc = (port.description or "").lower()
        dev  = (port.device    or "").lower()
        if any(kw in desc for kw in ("arduino", "ch340", "ft232", "atmega")):
            return port.device
        if "ttyacm" in dev or "ttyusb" in dev:
            return port.device
    return None
```

---

## Data Parser

```python
# python/feature_extractor.py

from dataclasses import dataclass, field
from typing import Optional
import time

@dataclass
class RespiratoryFeatures:
    breathing_rate:  float          # Hz
    spectral_power:  float          # arbitrary
    regularity:      float          # 0–1
    ie_ratio:        float          # dimensionless
    apnea_flag:      int            # 0 or 1
    timestamp:       float          # time.time()
    # Optional extension fields — None if not fitted
    spo2:            Optional[float] = None   # %
    heart_rate:      Optional[float] = None   # bpm

    @property
    def breathing_rate_bpm(self) -> float:
        return self.breathing_rate * 60.0


def parse_data_packet(line: str) -> Optional[RespiratoryFeatures]:
    """Parse a DATA,... line. Returns None on error or non-DATA lines."""
    if not line.startswith("DATA,"):
        return None
    try:
        parts = line.split(',')
        base = RespiratoryFeatures(
            breathing_rate = float(parts[1]),
            spectral_power = float(parts[2]),
            regularity     = float(parts[3]),
            ie_ratio       = float(parts[4]),
            apnea_flag     = int(parts[5]),
            timestamp      = time.time(),
        )
        # Extension fields if present
        if len(parts) >= 8:
            base.spo2       = float(parts[6])
            base.heart_rate = float(parts[7])
        return base
    except (ValueError, IndexError):
        return None
```

---

## Auto Port Detection in main.py

```python
# python/main.py  (simplified)

from serial_handler import SerialHandler, find_arduino_port
import sys

def main():
    port = find_arduino_port()
    if port is None:
        print("Arduino not found. Check USB cable and driver.")
        sys.exit(1)

    print(f"Found Arduino on {port}")
    handler = SerialHandler(port)
    if not handler.connect():
        sys.exit(1)

    # ... initialise classifier, LLM, GUI
    # handler.disconnect() called on exit

if __name__ == "__main__":
    main()
```

---

## Debugging Tips

| Symptom | Cause | Fix |
|---|---|---|
| Port not found by auto-detect | Driver not installed | Install CH340 or FTDI driver for your OS |
| Data arrives garbled | Baud rate mismatch | Confirm `Serial.begin(115200)` in firmware |
| No data at all | Another program has the port open | Close Arduino Serial Monitor before running Python |
| First few packets are garbage | Arduino still resetting | The `time.sleep(2.0)` handles this; increase to 3.0 if needed |
| Queue fills up (dropped packets) | GUI processing too slow | Increase `maxsize`, or reduce FFT packet rate |
| `CMD:` commands appear ignored | Arduino not in `checkSerialRx()` path | Verify `checkSerialRx()` is called every `loop()` iteration |