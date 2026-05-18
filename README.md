# Respiratory Fitness Monitor

A real-time respiratory health and fitness monitoring system built on an Arduino Uno. The system captures breathing patterns from two analogue sensors, processes them with FFT on the microcontroller, streams feature data to a Python application, and applies machine learning classification and LLM-based natural language interpretation to provide live feedback to the user.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     SENSOR LAYER (core)                         │
│       Piezo film sensor (chest belt)  │  NTC thermistor (nasal) │
└───────────────┬─────────────────────────────┬───────────────────┘
                │                             │
                ▼                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                  ANALOGUE FRONT-END (breadboard)                │
│  Voltage follower → Bandpass filter → Gain stage → DC bias      │
│                Voltage divider → RC low-pass filter             │
└───────────────┬─────────────────────────────┬───────────────────┘
                │ A0                           │ A1
                ▼                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    ARDUINO UNO (ATmega328P)                     │
│  100 Hz ADC sampling  │  256-pt rolling FFT  │  Feature extract │
│  Apnea detection      │  Serial TX / RX                         │
└────────────────────────────────┬────────────────────────────────┘
                                 │ USB Serial 115200 baud
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                        PYTHON (PC)                              │
│  pyserial RX/TX  │  Feature parsing  │  Rolling spectrogram     │
│  Scikit-learn Random Forest classifier                          │
│  Claude LLM API — natural language interpretation               │
│  pyqtgraph live dashboard  │  Session report generator         │
└────────────────────────────────┬────────────────────────────────┘
                                 │ Commands back to Arduino
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                       OUTPUT LAYER                              │
│  RGB LED (severity colour)  │  Pacer LED (breathing guide)      │
│  Buzzer (apnea alert)       │  LCD 16×2 (live metrics)          │
└─────────────────────────────────────────────────────────────────┘

     ┌────────────────────────────────────────────────────┐
     │  OPTIONAL EXTENSION — Heart Rate / SpO2            │
     │  Option A (~£1):  BPW34 photodiode + 940nm IR LED  │
     │  Option B (~£2–3): GY-MAX30100 I2C module           │
     └────────────────────────────────────────────────────┘
```

---

## Repository Structure

```
respiratory-monitor/
│
├── README.md
│
├── arduino/
│   └── respiratory_monitor/
│       └── respiratory_monitor.ino
│
├── python/
│   ├── main.py
│   ├── serial_handler.py
│   ├── feature_extractor.py
│   ├── classifier.py
│   ├── llm_client.py
│   ├── gui.py
│   ├── collect_training_data.py
│   ├── train_classifier.py
│   └── models/
│       └── respiratory_rf.pkl
│
└── docs/
```

---

## Core Features

- **Dual-channel analogue sensing** — piezo chest belt capturing waveform morphology, nasal thermistor detecting airflow presence and rate, both sampled at 100 Hz
- **Real-time FFT** — 256-point rolling FFT giving 0.39 Hz frequency resolution across the full clinical respiratory band
- **Pattern classification** — scikit-learn Random Forest trained on labelled sessions; classifies breathing states including normal, tachypnea, bradypnea, exercise intensity levels, irregular, and recovery
- **LLM interpretation** — Claude API converts structured metrics into natural language commentary and adaptive biofeedback recommendations
- **Bidirectional serial protocol** — Arduino streams feature data up; Python sends command codes back (severity, biofeedback rate, display text, alerts)
- **Context-aware modes** — Exercise, Stress, and Passive monitor modes selectable from the GUI; same hardware throughout

---

## Quick Start

### Arduino

1. Install Arduino IDE 2.x
2. Install via Library Manager: `arduinoFFT`, `LiquidCrystal_I2C`
3. If using the SpO2 extension: also install `SparkFun MAX3010x`
4. Open `arduino/respiratory_monitor/respiratory_monitor.ino`
5. Select Board: Arduino Uno, correct port — upload

### Python

```bash
pip install pyserial numpy scipy scikit-learn pyqtgraph PyQt5 anthropic
export ANTHROPIC_API_KEY=your_key_here   # Linux/Mac
# set ANTHROPIC_API_KEY=your_key_here   # Windows CMD
python python/main.py
```

---