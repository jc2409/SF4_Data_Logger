# 08 — Python GUI

## Overview

The dashboard is built with **PyQt5** and **pyqtgraph**. PyQtgraph renders plots at > 60 fps without blocking the GUI — matplotlib cannot do this for live streaming data. The layout has four panels: live waveform, rolling spectrogram, metrics summary, and LLM commentary feed.

---

## Installation

```bash
pip install pyqtgraph PyQt5 numpy
```

---

## Layout

```
┌──────────────────────────┬──────────────────────────────────┐
│  LIVE WAVEFORM           │  METRICS                         │
│  (piezo channel, A0)     │  Breathing rate:  14.2 br/min    │
│  scrolling 2.56-s window │  Regularity:      0.74           │
│                          │  I:E ratio:       0.58           │
│                          │  Apnea:           No             │
├──────────────────────────│  SpO2:            — (extension)  │
│  SPECTROGRAM             │  Heart rate:      — (extension)  │
│  (time × frequency)      │  Pattern:  NORMAL (81%)          │
│  colour = power          │                                  │
│  y-axis 0–4 Hz           ├──────────────────────────────────┤
│                          │  SEVERITY   ●●○○  Mild concern   │
│                          │  Biofeedback: 6 br/min  ●        │
├──────────────────────────├──────────────────────────────────┤
│  [▶ Start] [■ Stop]      │  AI COMMENTARY                   │
│  Mode: [EXERCISE ▼]      │  Rate elevated at 28 br/min,     │
│  [⬇ Export Report]       │  consistent with moderate effort.│
│                          │  ─────────────────────────────   │
│                          │  ▶ Maintain current pace.        │
└──────────────────────────┴──────────────────────────────────┘
```

---

## Full GUI Implementation

```python
# python/gui.py

import sys, time
import numpy as np
import pyqtgraph as pg
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget,
    QVBoxLayout, QHBoxLayout, QLabel,
    QPushButton, QComboBox, QTextEdit, QGridLayout
)
from PyQt5.QtCore import QTimer, Qt
from PyQt5.QtGui import QFont

from serial_handler import SerialHandler
from feature_extractor import parse_data_packet
from classifier import BreathingClassifier
from llm_client import LLMClient, apply_llm_response

SEVERITY_COLOURS = {0: "#27ae60", 1: "#e67e22", 2: "#e74c3c", 3: "#c0392b"}
SEVERITY_LABELS  = {0: "Normal", 1: "Mild concern", 2: "Alert", 3: "Urgent"}


class Dashboard(QMainWindow):
    def __init__(self, serial: SerialHandler, clf: BreathingClassifier, llm: LLMClient):
        super().__init__()
        self.serial  = serial
        self.clf     = clf
        self.llm     = llm
        self.mode    = "EXERCISE"
        self.running = False
        self.session_start = None

        # Data buffers
        self.waveform  = np.zeros(256)
        self.br_hist   = []   # breathing rate over session
        self.t_hist    = []   # timestamps
        self.spec_buf  = np.zeros((16, 60))  # 16 freq bins × 60 time steps

        pg.setConfigOption('background', '#1a1a2e')
        pg.setConfigOption('foreground', '#e0e0e0')

        self._build_ui()
        self._start_timers()

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        self.setWindowTitle("Respiratory Fitness Monitor")
        self.setMinimumSize(1050, 680)

        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)

        left  = QVBoxLayout()
        right = QVBoxLayout()
        root.addLayout(left,  stretch=3)
        root.addLayout(right, stretch=2)

        self._add_waveform(left)
        self._add_spectrogram(left)
        self._add_controls(left)
        self._add_metrics(right)
        self._add_llm_panel(right)

    def _add_waveform(self, layout):
        self.wave_plot = pg.PlotWidget(title="Live Breathing Waveform (piezo)")
        self.wave_plot.setLabel('left',   'ADC (zero-centred)')
        self.wave_plot.setLabel('bottom', 'Sample (100 Hz)')
        self.wave_plot.setYRange(-512, 512)
        self.wave_curve = self.wave_plot.plot(pen=pg.mkPen('#4fc3f7', width=2))
        layout.addWidget(self.wave_plot)

    def _add_spectrogram(self, layout):
        self.spec_plot = pg.PlotWidget(title="Rolling Spectrogram (0–4 Hz)")
        self.spec_plot.setLabel('left',   'Frequency (Hz)')
        self.spec_plot.setLabel('bottom', 'Time →')
        self.spec_img  = pg.ImageItem()
        self.spec_plot.addItem(self.spec_img)
        self.spec_img.setColorMap(pg.colormap.get('CET-L9'))
        self.spec_plot.setYRange(0, 4)
        layout.addWidget(self.spec_plot)

    def _add_controls(self, layout):
        row = QHBoxLayout()

        self.mode_cb = QComboBox()
        self.mode_cb.addItems(["EXERCISE", "STRESS", "PASSIVE"])
        self.mode_cb.currentTextChanged.connect(lambda m: setattr(self, 'mode', m))
        row.addWidget(QLabel("Mode:"))
        row.addWidget(self.mode_cb)

        self.btn_start = QPushButton("▶  Start")
        self.btn_start.setStyleSheet("background:#27ae60;color:white;font-weight:bold;padding:8px")
        self.btn_start.clicked.connect(self._on_start)
        row.addWidget(self.btn_start)

        self.btn_stop = QPushButton("■  Stop")
        self.btn_stop.setEnabled(False)
        self.btn_stop.clicked.connect(self._on_stop)
        row.addWidget(self.btn_stop)

        btn_export = QPushButton("⬇  Export")
        btn_export.clicked.connect(self._on_export)
        row.addWidget(btn_export)

        layout.addLayout(row)

    def _add_metrics(self, layout):
        layout.addWidget(self._separator("── Live Metrics ──"))

        grid = QGridLayout()
        self._mv = {}
        rows = [
            ("Breathing rate", "—"),
            ("Regularity",     "—"),
            ("I:E ratio",      "—"),
            ("Apnea",          "—"),
            ("SpO₂",           "— (ext.)"),
            ("Heart rate",     "— (ext.)"),
            ("Pattern",        "—"),
        ]
        for i, (lbl, default) in enumerate(rows):
            ql = QLabel(lbl);   ql.setFont(QFont("Arial", 10))
            qv = QLabel(default); qv.setFont(QFont("Courier", 16))
            qv.setStyleSheet("color:#4fc3f7")
            grid.addWidget(ql, i, 0)
            grid.addWidget(qv, i, 1)
            self._mv[lbl] = qv
        layout.addLayout(grid)

        layout.addWidget(self._separator("── Status ──"))
        self.sev_label = QLabel("● Normal")
        self.sev_label.setFont(QFont("Arial", 13, QFont.Bold))
        self.sev_label.setStyleSheet(f"color:{SEVERITY_COLOURS[0]}")
        layout.addWidget(self.sev_label)

        self.bf_label = QLabel("Biofeedback: OFF")
        self.bf_label.setFont(QFont("Arial", 11))
        layout.addWidget(self.bf_label)

    def _add_llm_panel(self, layout):
        layout.addWidget(self._separator("── AI Commentary ──"))
        self.llm_box = QTextEdit()
        self.llm_box.setReadOnly(True)
        self.llm_box.setFont(QFont("Arial", 11))
        self.llm_box.setStyleSheet(
            "background:#12122a;color:#e0e0e0;border:1px solid #333;padding:4px"
        )
        self.llm_box.setMinimumHeight(160)
        layout.addWidget(self.llm_box)

        self.advice_label = QLabel("")
        self.advice_label.setWordWrap(True)
        self.advice_label.setFont(QFont("Arial", 11, QFont.Bold))
        self.advice_label.setStyleSheet("color:#f39c12;padding:6px")
        layout.addWidget(self.advice_label)

    def _separator(self, text: str) -> QLabel:
        lbl = QLabel(text)
        lbl.setFont(QFont("Arial", 9))
        lbl.setStyleSheet("color:#888;margin-top:6px")
        return lbl

    # ── Timers ───────────────────────────────────────────────────────────────

    def _start_timers(self):
        self._serial_timer = QTimer()
        self._serial_timer.timeout.connect(self._drain_serial)
        self._serial_timer.start(100)   # poll serial every 100 ms

        self._plot_timer = QTimer()
        self._plot_timer.timeout.connect(self._refresh_plots)
        self._plot_timer.start(200)     # redraw every 200 ms

    # ── Session controls ─────────────────────────────────────────────────────

    def _on_start(self):
        self.running       = True
        self.session_start = time.time()
        self.btn_start.setEnabled(False)
        self.btn_stop.setEnabled(True)
        self.llm_box.clear()
        self.llm_box.append(f"Session started — mode: {self.mode}\n")

    def _on_stop(self):
        self.running = False
        self.btn_start.setEnabled(True)
        self.btn_stop.setEnabled(False)
        for cmd in ("CMD:SEV:0", "CMD:BF:0", "CMD:ALERT:0"):
            self.serial.send(cmd)

    def _on_export(self):
        summary  = self.llm.session_summary()
        filename = f"session_{int(time.time())}.txt"
        with open(filename, 'w') as f:
            f.write(summary)
        self.llm_box.append(f"\n[Report saved → {filename}]")

    # ── Data handling ─────────────────────────────────────────────────────────

    def _drain_serial(self):
        while True:
            msg = self.serial.get_message()
            if msg is None:
                break
            self._handle_message(msg)

    def _handle_message(self, msg: str):
        feat = parse_data_packet(msg)
        if feat is None:
            return

        bpm = feat.breathing_rate * 60.0
        self._mv["Breathing rate"].setText(f"{bpm:.1f} br/min")
        self._mv["Regularity"].setText(f"{feat.regularity:.2f}")
        self._mv["I:E ratio"].setText(f"{feat.ie_ratio:.2f}")
        self._mv["Apnea"].setText("⚠ YES" if feat.apnea_flag else "No")

        if feat.spo2 is not None:
            self._mv["SpO₂"].setText(f"{feat.spo2:.1f}%")
            self._mv["Heart rate"].setText(f"{feat.heart_rate:.0f} bpm")

        if feat.apnea_flag:
            self.serial.send("CMD:ALERT:1")
            self.llm_box.append("\n⚠ APNEA EVENT DETECTED")

        if not self.running:
            return

        result  = self.clf.predict(feat)
        cls_lbl = result['class'].replace('_', ' ').title()
        conf    = result['confidence'] * 100
        self._mv["Pattern"].setText(f"{cls_lbl} ({conf:.0f}%)")

        elapsed = (time.time() - self.session_start) / 60.0
        self.br_hist.append(bpm)
        self.t_hist.append(elapsed)

        llm_resp = self.llm.interpret(feat, result, self.mode, elapsed)
        if llm_resp:
            self._apply_llm(llm_resp)

    def _apply_llm(self, resp):
        self.llm_box.append(f"\n{resp.commentary}")
        self.advice_label.setText(f"▶ {resp.advice}")

        colour = SEVERITY_COLOURS.get(resp.severity, SEVERITY_COLOURS[0])
        self.sev_label.setText(f"● {SEVERITY_LABELS[resp.severity]}")
        self.sev_label.setStyleSheet(f"color:{colour}")

        self.bf_label.setText(
            f"Biofeedback: {resp.biofeedback_rate} br/min ●"
            if resp.biofeedback_rate > 0 else "Biofeedback: OFF"
        )
        apply_llm_response(resp, self.serial)

    def _refresh_plots(self):
        self.wave_curve.setData(self.waveform)
        if self.br_hist:
            self.spec_buf = np.roll(self.spec_buf, -1, axis=1)
            # Spectrogram column populated from FFT bins if sent by Arduino
            self.spec_img.setImage(self.spec_buf.T, autoLevels=False, levels=(0, 300))

    def run(self):
        app = QApplication.instance() or QApplication(sys.argv)
        self.show()
        sys.exit(app.exec_())
```

---

## Extending the Spectrogram

To populate the spectrogram meaningfully, extend the Arduino serial packet to include 16 FFT bins:

```
DATA,<br>,<power>,<reg>,<ie>,<apnea>,<b1>,<b2>,...,<b16>\n
```

In the Python parser:

```python
if len(parts) >= 22:
    spectrum = [float(parts[6 + i]) for i in range(16)]
    # Insert as new column in spectrogram buffer
    self.spec_buf[:, -1] = spectrum
```

This adds ~80 bytes per packet (16 short integers as ASCII), well within the 115200 baud capacity.