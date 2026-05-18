# Arduino Firmware

## Overview

The firmware performs four tasks using a timer-interrupt architecture:

1. **ADC sampling** at exactly 100 Hz on A0 (piezo) and A1 (thermistor) via Timer1 ISR
2. **Rolling FFT** on the piezo channel every 2.56 seconds (256 samples)
3. **Apnea detection** from the thermistor channel (continuous)
4. **Serial communication** — streaming feature packets to Python, parsing command packets back

If the SpO2 extension is fitted, I2C polling of the MAX30100/30102 is added as a fifth task — see the extension section at the end.

---

## Library Installation

Open Arduino IDE → Tools → Manage Libraries:

| Library | Author | Purpose |
|---|---|---|
| `arduinoFFT` | Enrique Blasina | FFT computation |
| `LiquidCrystal_I2C` | Frank de Brabander | LCD over I2C |
| `MAX30100lib` *(extension only)* | OXullo Intersecans | GY-MAX30100 driver |
| `SparkFun MAX3010x` *(extension only)* | SparkFun | MAX30102 driver |

---

## Firmware Architecture

```
setup()
  ├── Serial.begin(115200)
  ├── Wire.begin()                    ← only needed if LCD or SpO2 extension fitted
  ├── lcd.begin() / lcd.backlight()
  ├── Timer1 interrupt configured at 100 Hz
  └── Pin modes: D6 (pacer), D7 (buzzer), D9/D10/D11 (RGB)

loop()
  ├── if (sampleReady)
  │     └── store sample, check apnea on thermistor
  ├── if (fftWindowFull)              ← every 256 samples = 2.56 s
  │     ├── computeFFT()
  │     ├── extractFeatures()
  │     └── sendSerialPacket()
  ├── checkSerialRx()                 ← parse CMD: packets from Python
  └── updateOutputs()                 ← RGB LED, pacer LED, buzzer, LCD

ISR (Timer1 @ 100 Hz)
  ├── analogRead(A0) → piezoBuffer[idx]
  ├── analogRead(A1) → thermoBuffer[idx]
  ├── idx++
  └── set sampleReady flag
```

---

## Timer1 — Exact 100 Hz Sampling

Do not use `delay()` or `millis()` for sampling — they drift and distort FFT frequency accuracy. Use Timer1 in CTC mode to fire an ISR at precisely 100 Hz.

```cpp
#include <avr/interrupt.h>

// Sampling buffers
#define SAMPLES 256
volatile int16_t  piezoBuffer[SAMPLES];   // int16_t saves RAM vs uint16_t
volatile uint16_t thermoBuffer[SAMPLES];
volatile uint8_t  bufferIdx  = 0;
volatile bool     sampleReady = false;
volatile bool     windowFull  = false;

void setupTimer1() {
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  // 16 MHz / (256 prescaler × 100 Hz) − 1 = 624
  OCR1A  = 624;
  TCCR1B |= (1 << WGM12);   // CTC mode
  TCCR1B |= (1 << CS12);    // prescaler 256
  TIMSK1 |= (1 << OCIE1A);  // enable compare match interrupt
  sei();
}

ISR(TIMER1_COMPA_vect) {
  if (bufferIdx < SAMPLES) {
    // Read both channels back-to-back
    // Each analogRead takes ~104 µs — at 100 Hz ISR period (10 ms) this is fine
    int raw = analogRead(A0);
    piezoBuffer[bufferIdx]  = raw - 512;        // remove DC bias (512 = midpoint)
    thermoBuffer[bufferIdx] = analogRead(A1);
    bufferIdx++;
    sampleReady = true;
    if (bufferIdx == SAMPLES) windowFull = true;
  }
}
```

**Why subtract 512?** Removing the DC component before FFT prevents the zero-frequency bin from dominating the spectrum and masking small breathing-frequency peaks. The bias circuit centres the piezo signal at 512 ADC counts (2.5V), so subtracting 512 gives a zero-mean signal suitable for FFT.

---

## FFT Implementation

```cpp
#include <arduinoFFT.h>

#define SAMPLING_FREQ 100.0f

// Use float (not double) to fit within 2 KB SRAM
float vReal[SAMPLES];
float vImag[SAMPLES];

arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

void computeFFT() {
  // Copy piezo buffer into vReal (already DC-removed in ISR)
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (float)piezoBuffer[i];
    vImag[i] = 0.0f;
  }

  FFT.Windowing(FFT_WIN_TYP_HANN, FFT_FORWARD);  // reduce spectral leakage
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();

  // vReal[0..127] now holds the one-sided magnitude spectrum
  // Frequency resolution: 100 / 256 = 0.39 Hz per bin
  // Breathing band: 0.1–1.0 Hz = bins 0–2 (skip bin 0 = DC residual)
}
```

**Memory note:** `float vReal[256]` + `float vImag[256]` = 2048 bytes. Add `piezoBuffer` (512 bytes) and `thermoBuffer` (512 bytes) = 3072 bytes total — exceeding the ATmega328P's 2048-byte SRAM. Solution: after the ISR fills `piezoBuffer`, copy it to `vReal` inside `computeFFT()`, then reuse `piezoBuffer` for the next window. The arrays are never simultaneously needed at full occupancy. If memory is still tight, reduce to 128 samples (1.28-second window, 0.78 Hz resolution) — still sufficient for breathing analysis.

---

## Feature Extraction

```cpp
struct Features {
  float breathingRate;    // Hz — dominant FFT peak in 0.1–1.5 Hz
  float spectralPower;    // sum of squared magnitudes in breathing band
  float regularity;       // 0–1, sharpness of dominant peak
  float ieRatio;          // inspiratory:expiratory ratio
  uint8_t apneaFlag;      // 1 if no breath for >10 s
  // SpO2 / HR added here if extension fitted
};

Features extractFeatures() {
  Features f;

  // Find dominant bin in breathing band (bins 1–8 = 0.39–3.12 Hz)
  float maxMag    = 0;
  float totalPow  = 0;
  int   peakBin   = 1;

  for (int k = 1; k <= 8; k++) {
    float mag = vReal[k];
    if (mag > maxMag) { maxMag = mag; peakBin = k; }
    totalPow += mag * mag;
  }

  f.breathingRate   = peakBin * (SAMPLING_FREQ / SAMPLES);
  f.spectralPower   = totalPow;
  f.regularityScore = (maxMag * maxMag) / max(totalPow, 1.0f);

  // I:E ratio — fraction of waveform above vs below zero (zero = exhale baseline)
  int above = 0, below = 0;
  for (int i = 0; i < SAMPLES; i++) {
    if (piezoBuffer[i] > 0) above++; else below++;
  }
  f.ieRatio = (float)above / max(below, 1);

  f.apneaFlag = apneaActive ? 1 : 0;

  return f;
}
```

---

## Apnea Detection

```cpp
#define APNEA_THRESHOLD  20    // ADC counts — tune experimentally (start at 20)
#define APNEA_TIMEOUT_S  10    // seconds with no breath before flagging

bool     apneaActive     = false;
uint16_t apneaCounter    = 0;  // counts 100-Hz samples with low thermistor swing

// Called from the ISR or from loop() after each sample
void checkApnea(uint16_t thermoSample) {
  static uint16_t thermoMin = 1023, thermoMax = 0;
  static uint8_t  windowCount = 0;

  if (thermoSample < thermoMin) thermoMin = thermoSample;
  if (thermoSample > thermoMax) thermoMax = thermoSample;

  if (++windowCount >= 200) {  // evaluate every 2 seconds
    uint16_t swing = thermoMax - thermoMin;
    thermoMin = 1023; thermoMax = 0; windowCount = 0;

    if (swing < APNEA_THRESHOLD) {
      apneaCounter += 200;
      if (apneaCounter >= APNEA_TIMEOUT_S * 100 && !apneaActive) {
        apneaActive = true;
        Serial.println(F("DATA:APNEA:1"));
      }
    } else {
      apneaCounter = 0;
      if (apneaActive) {
        apneaActive = false;
        Serial.println(F("DATA:APNEA:0"));
      }
    }
  }
}
```

---

## Serial Communication Protocol

### Outgoing — Feature Packet (Arduino → Python)

Sent every 2.56 seconds after each FFT window:

```
DATA,<breathingRate>,<spectralPower>,<regularity>,<ieRatio>,<apneaFlag>\n
```

Example:
```
DATA,0.47,1842.3,0.73,0.61,0
```

If the SpO2 extension is fitted, two extra fields are appended:
```
DATA,0.47,1842.3,0.73,0.61,0,98.2,72
```

### Incoming — Command Packet (Python → Arduino)

```
CMD:<TYPE>:<VALUE>\n
```

| Command | Example | Arduino action |
|---|---|---|
| `CMD:SEV:<0-3>` | `CMD:SEV:2` | Set RGB LED colour |
| `CMD:BF:<bpm>` | `CMD:BF:6` | Set pacer LED rate (0 = off) |
| `CMD:LCD1:<text>` | `CMD:LCD1:BR 14  SpO2 98` | Write LCD row 0 |
| `CMD:LCD2:<text>` | `CMD:LCD2:NORMAL       ` | Write LCD row 1 |
| `CMD:ALERT:<0-1>` | `CMD:ALERT:1` | Buzzer on/off |

```cpp
void checkSerialRx() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if      (cmd.startsWith(F("CMD:SEV:")))   handleSeverity(cmd.charAt(8) - '0');
  else if (cmd.startsWith(F("CMD:BF:")))    handleBiofeedback(cmd.substring(7).toInt());
  else if (cmd.startsWith(F("CMD:ALERT:"))) handleAlert(cmd.charAt(10) - '0');
  else if (cmd.startsWith(F("CMD:LCD1:")))  { lcd.setCursor(0,0); lcd.print(cmd.substring(9)); }
  else if (cmd.startsWith(F("CMD:LCD2:")))  { lcd.setCursor(0,1); lcd.print(cmd.substring(9)); }
}
```

Use the `F()` macro on all string literals to store them in flash (program memory) rather than SRAM — critical for staying within the 2 KB SRAM limit.

---

## Sending the Feature Packet

```cpp
void sendSerialPacket(Features& f) {
  Serial.print(F("DATA,"));
  Serial.print(f.breathingRate, 3);  Serial.print(',');
  Serial.print(f.spectralPower, 1);  Serial.print(',');
  Serial.print(f.regularity,    3);  Serial.print(',');
  Serial.print(f.ieRatio,       3);  Serial.print(',');
  Serial.print(f.apneaFlag);
  // Append SpO2/HR if extension fitted:
  // Serial.print(','); Serial.print(latestSpO2, 1);
  // Serial.print(','); Serial.print((int)latestHR);
  Serial.println();  // \n terminator
}
```

---

## Main Loop Structure

```cpp
void loop() {
  // 1. Process new samples from ISR
  if (sampleReady) {
    sampleReady = false;
    checkApnea(thermoBuffer[bufferIdx > 0 ? bufferIdx - 1 : 0]);
  }

  // 2. When a full 256-sample window is ready, run FFT
  if (windowFull) {
    windowFull = false;
    bufferIdx  = 0;

    computeFFT();
    Features f = extractFeatures();
    sendSerialPacket(f);

    // Update LCD locally (Python can override with CMD:LCD1/2)
    char line1[17];
    snprintf(line1, 17, "BR%-4.1f         ", f.breathingRate * 60.0f);
    lcd.setCursor(0, 0); lcd.print(line1);
  }

  // 3. Parse any incoming Python commands
  checkSerialRx();

  // 4. Update outputs (non-blocking — see doc 09)
  updateRGBFlash();    // flashing for severity 3
  updatePacer();       // smooth cosine fade on D6
  updateBuzzer();      // beep patterns on D7
}
```

---

## Optional Extension — SpO2 with GY-MAX30100

Add after core system is verified working. The GY-MAX30100 uses I2C and the `MAX30100lib` library.

```cpp
// Add to firmware if GY-MAX30100 extension fitted
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"

PulseOximeter pox;
float latestSpO2 = 0;
float latestHR   = 0;

void initMAX30100() {
  if (!pox.begin()) {
    Serial.println(F("ERR:MAX30100 not found"));
    while(1);
  }
  pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
}

// Call from loop() — the library manages its own timing internally
void pollMAX30100() {
  pox.update();
  latestSpO2 = pox.getSpO2();
  latestHR   = pox.getHeartRate();
}
```

I2C wiring: Arduino A4 (SDA) → MAX30100 SDA, Arduino A5 (SCL) → MAX30100 SCL, Arduino 3.3V → MAX30100 VCC, GND → GND. The GY-MAX30100 module has on-board pull-up resistors.