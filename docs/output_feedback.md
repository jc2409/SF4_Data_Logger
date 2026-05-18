# Output and Feedback Hardware

## Overview

Four physical outputs feed back information to the user without requiring the PC screen — useful during exercise.

| Output | Pin | Function |
|---|---|---|
| RGB LED | D9 (R), D10 (G), D11 (B) | Severity colour indicator |
| Pacer LED (green) | D6 (PWM) | Biofeedback breathing guide |
| Buzzer (active, 5V) | D7 | Apnea / urgent alert |
| LCD 16×2 with I2C | A4/A5 (I2C) | Live BR display and status text |

---

## 1. RGB LED

### Wiring

```
Arduino D9  ──── [220 Ω] ──── RGB LED Red pin
Arduino D10 ──── [150 Ω] ──── RGB LED Green pin
Arduino D11 ──── [150 Ω] ──── RGB LED Blue pin
RGB LED cathode (longest pin) ──── GND
```

**Resistor calculation** (target ~15 mA per channel):
- Red Vf ≈ 2.1V: (5 − 2.1) / 0.015 = 193 Ω → use **220 Ω**
- Green Vf ≈ 3.1V: (5 − 3.1) / 0.015 = 127 Ω → use **150 Ω**
- Blue Vf ≈ 3.1V: (5 − 3.1) / 0.015 = 127 Ω → use **150 Ω**

Using the same resistor for all three would give different perceived brightness; these values normalise the current across channels.

### Firmware

```cpp
const uint8_t SEVERITY_R[] = {0,   255, 255, 255};
const uint8_t SEVERITY_G[] = {200, 165, 0,   0  };
const uint8_t SEVERITY_B[] = {0,   0,   0,   0  };
// Colours: 0=green, 1=amber, 2=red, 3=red (handled by flash)

uint8_t currentSeverity = 0;

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(9,  r);
  analogWrite(10, g);
  analogWrite(11, b);
}

void handleSeverity(uint8_t level) {
  currentSeverity = level;
  if (level < 3) {
    setRGB(SEVERITY_R[level], SEVERITY_G[level], SEVERITY_B[level]);
  }
  // Level 3 is handled by updateRGBFlash() in loop()
}

// Call from loop() — non-blocking 2 Hz flash for severity 3
uint32_t lastFlash  = 0;
bool     flashState = false;
void updateRGBFlash() {
  if (currentSeverity != 3) return;
  if (millis() - lastFlash > 250) {
    flashState = !flashState;
    setRGB(flashState ? 255 : 0, 0, 0);
    lastFlash = millis();
  }
}
```

---

## 2. Pacer LED — Biofeedback Breathing Guide

A smooth cosine-wave pulse is far more natural to follow than an abrupt on/off flash.

### Wiring

```
Arduino D6 ──── [150 Ω] ──── Green LED anode
Green LED cathode ──── GND
```

D6 supports PWM (`analogWrite()`).

### Firmware — Cosine Fade

```cpp
uint8_t  pacerBPM      = 0;
uint32_t pacerPeriodMs = 0;
uint32_t pacerStartMs  = 0;

void handleBiofeedback(uint8_t bpm) {
  pacerBPM = bpm;
  if (bpm == 0) { analogWrite(6, 0); return; }
  pacerPeriodMs = 60000UL / bpm;
  pacerStartMs  = millis();
}

// Call from loop() every iteration — no delays
void updatePacer() {
  if (pacerBPM == 0) return;
  uint32_t elapsed = (millis() - pacerStartMs) % pacerPeriodMs;
  float    phase   = (float)elapsed / pacerPeriodMs;  // 0.0–1.0
  // Cosine: bright at phase=0 (inhale peak), dim at phase=0.5 (exhale)
  float brightness = 0.5f * (1.0f - cosf(TWO_PI * phase));
  analogWrite(6, (uint8_t)(brightness * 190));  // cap at 190/255
}
```

At 6 br/min (biofeedback for relaxation), `pacerPeriodMs` = 10,000 ms — a full 10-second fade cycle, which the user can clearly see and synchronise to.

---

## 3. Buzzer

### Wiring

Use an **active** (self-drive) piezo buzzer — it only needs a DC HIGH to sound. A passive buzzer requires a PWM tone and is harder to use here.

```
Arduino D7 ──── Buzzer positive terminal
Buzzer negative terminal ──── GND
```

If the buzzer draws > 20 mA (check datasheet), drive it via a 2N2222 NPN transistor:

```
D7 ──── [1 kΩ] ──── transistor base (pin B)
Transistor collector (pin C) ──── buzzer + terminal ──── 5V
Transistor emitter (pin E) ──── GND
```

### Firmware — Alert Patterns

```cpp
enum AlertPattern { ALERT_OFF = 0, ALERT_APNEA = 1, ALERT_URGENT = 2 };
AlertPattern alertMode   = ALERT_OFF;
uint32_t     alertTimer  = 0;
bool         buzzerOn    = false;
uint8_t      beepCount   = 0;

void handleAlert(uint8_t pattern) {
  alertMode = (AlertPattern)pattern;
  if (pattern == 0) { digitalWrite(7, LOW); buzzerOn = false; }
}

void updateBuzzer() {
  uint32_t now = millis();
  switch (alertMode) {
    case ALERT_OFF: break;

    case ALERT_APNEA:
      // 3 short beeps (200 ms on, 300 ms off) then 2.5 s silence, repeat
      if (beepCount < 3) {
        if (!buzzerOn && now - alertTimer > 300) {
          digitalWrite(7, HIGH); buzzerOn = true; alertTimer = now;
        } else if (buzzerOn && now - alertTimer > 200) {
          digitalWrite(7, LOW);  buzzerOn = false; alertTimer = now; beepCount++;
        }
      } else if (now - alertTimer > 2500) {
        beepCount = 0; alertTimer = now;
      }
      break;

    case ALERT_URGENT:
      // Fast continuous beep at 5 Hz
      if (now - alertTimer > 100) {
        buzzerOn = !buzzerOn;
        digitalWrite(7, buzzerOn ? HIGH : LOW);
        alertTimer = now;
      }
      break;
  }
}
```

---

## 4. LCD 16×2 with I2C Backpack

### Wiring

```
Arduino 5V  ──── LCD VCC
Arduino GND ──── LCD GND
Arduino A4  ──── LCD SDA   (shared I2C bus)
Arduino A5  ──── LCD SCL   (shared I2C bus)
```

The I2C backpack's address is 0x27 (most common) or 0x3F. Run the I2C scanner sketch to confirm.

### Firmware

```cpp
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);

void initLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0); lcd.print(F("Resp Monitor    "));
  lcd.setCursor(0,1); lcd.print(F("Initialising... "));
}

// Update after each FFT window with live values
// Python can override via CMD:LCD1: and CMD:LCD2:
void updateLCDLocal(float brBPM, bool apnea) {
  char row0[17], row1[17];
  snprintf(row0, 17, "BR %-4.1f br/min  ", brBPM);
  snprintf(row1, 17, apnea ? "!!! APNEA !!!   " : "                ");
  lcd.setCursor(0,0); lcd.print(row0);
  lcd.setCursor(0,1); lcd.print(row1);
}
```

### I2C Address Scanner

If the LCD does not appear, run this once to find its address:

```cpp
#include <Wire.h>
void setup() {
  Wire.begin(); Serial.begin(115200);
  Serial.println(F("Scanning I2C..."));
  for (byte a = 8; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0)
      Serial.println(a, HEX);
  }
}
void loop() {}
```

---

## Power Budget

| Device | Current |
|---|---|
| RGB LED (all max) | 3 × 15 mA = 45 mA |
| Pacer LED | 15 mA |
| Buzzer | ≤ 20 mA |
| LCD backlight | ~20 mA |
| Op-amp (LM358) | < 5 mA |
| Thermistor divider | < 1 mA |
| **Total** | **~106 mA** |

Well within the USB 2.0 500 mA limit. Use a **200 mA polyfuse** on the 5V breadboard rail (a 100 mA fuse would blow on peak current).