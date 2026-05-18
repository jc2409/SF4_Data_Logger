# Analogue Circuit Design

## Overview

The analogue front-end conditions two independent input channels before they reach the Arduino ADC:

- **Channel A0 — Piezo sensor:** high-impedance voltage follower → active bandpass filter → gain stage → DC bias
- **Channel A1 — Thermistor:** voltage divider → passive RC low-pass filter

Both channels operate from the Arduino's 5V rail. A single LM358 dual op-amp IC handles both op-amp stages on the piezo channel. The thermistor channel needs no op-amp.

> **Op-amp choice:** Use the **LM358** (single-supply, 3–32V, from lab stock). Do **not** substitute the TL072 — its minimum supply is ±3.5V (7V total) and it will not operate from the Arduino's 5V rail.

If the SpO2 extension (Option A, BPW34 photodiode) is added later, a second LM358 is needed for the transimpedance amplifier — covered at the end of this document.

---

## Power Supply Architecture

```
Arduino 5V ──── polyfuse (200 mA) ──── breadboard +ve rail
                                             │
                             ├──── LM358 VCC (pin 8)
                             ├──── Voltage divider → 2.5V bias
                             └──── LED current sources

Arduino GND ──── breadboard −ve rail
                      │
                      └──── LM358 GND (pin 4), all signal grounds
```

Place a **100 nF ceramic decoupling capacitor** directly between pin 8 (VCC) and pin 4 (GND) of the LM358, physically adjacent to the IC. Without this, switching transients from the ATmega328P's internal clocking inject into the op-amp supply and appear as noise in the ADC readings.

---

## Channel A0 — Piezo Sensor Front-End

### Why the signal needs conditioning

The LDT1-028K is a capacitive (high-impedance) source. Its open-circuit voltage at breathing frequencies is typically 10–100 mV peak-to-peak, but the source impedance is in the tens of megaohms. Connecting it directly to the Arduino ADC causes severe attenuation because the ADC's effective input impedance is much lower than the source. The three-stage conditioning circuit solves this.

### Stage 1 — Voltage Follower (Buffer)

Op-amp A (LM358, pins 1, 2, 3), unity-gain configuration.

```
Piezo wire 1 ──── [R1: 1 MΩ] ──┬──── (+) in (pin 3)
                                │
                              [C1: 1 µF] ──── GND    ← HP corner: 0.16 Hz
                                │
Piezo wire 2 ──── GND       [R2: 1 MΩ] ──── GND    ← bias symmetry

(+) in (pin 3) ────────────────────────────────────── LM358A
(-) in (pin 2) ◄──────────────────── output (pin 1)  (unity gain feedback)
```

**R1 = 1 MΩ** forms a high-pass filter with C1: f_HP = 1/(2π × 1MΩ × 1µF) = **0.16 Hz**. This blocks slow DC drift from posture changes while passing the full breathing rate range (0.17–1.0 Hz = 10–60 br/min).

**R2 = 1 MΩ** on the non-inverting input matches the DC bias condition and minimises output offset due to input bias current.

**Why unity gain?** This stage is for impedance transformation only. The LM358 input impedance is ~1 GΩ — negligible loading on the piezo. The output impedance is < 1 Ω, able to drive the filter stage without signal loss.

### Stage 2 — Inverting Amplifier with Bandpass

Op-amp B (LM358, pins 5, 6, 7), inverting configuration.

```
Buffer output ──── [C2: 10 µF] ──── [R3: 2.2 kΩ] ──── (−) in (pin 6)
                                                              │
                                          [Rf: 100 kΩ] ──────┘──── output (pin 7)
                                                                        │
(+) in (pin 5) ──── 2.5V bias ──────────── [R_LP: 4.7 kΩ] ──────────┘
                                                │
                                            [C_LP: 100 nF] ──── GND
```

**C2 and R3 form the AC-coupling high-pass:** f_HP = 1/(2π × 2.2kΩ × 10µF) = **7.2 Hz** — this is intentionally aggressive; we only want breathing frequencies (< 2 Hz), not baseline wander from the first stage.

Wait — 7.2 Hz would cut the breathing signal entirely. Correct: use C2 = 100 µF for f_HP = 1/(2π × 2.2kΩ × 100µF) = **0.72 Hz**, or more practically keep the DC path open and rely on the Stage 1 HP filter at 0.16 Hz. Replace C2 with a direct wire and use only the Stage 1 HP filter. The inverting amplifier then operates with DC feedback:

```
Buffer output ──── [R3: 2.2 kΩ] ──── (−) in (pin 6) ──── [Rf: 100 kΩ] ──── output (pin 7)
                                                                (feedback)
```

**Gain:** Av = −Rf/Rin = −100 kΩ / 2.2 kΩ = **−45.5×** (inverting)

The 20 mV piezo signal becomes ~910 mV — well within the 0–5V ADC range.

**R_LP and C_LP** form the anti-aliasing low-pass filter on the output: f_LP = 1/(2π × 4.7kΩ × 100nF) = **338 Hz**. This is well above the breathing signal (< 2 Hz) but well below the 50 Hz Nyquist limit of the 100 Hz ADC, preventing any high-frequency noise from aliasing into the breathing band.

### DC Bias — Centring at 2.5V

The LM358 output swings around 0V with no bias. The Arduino ADC only accepts 0–5V, so without a DC offset the negative half of the breathing waveform is clipped to GND.

Set the non-inverting input (pin 5) to 2.5V:

```
5V ──── [R_B1: 10 kΩ] ──┬──── (+) in (pin 5)
                         │
                     [R_B2: 10 kΩ]
                         │
                        GND
                         │
                    [C_bypass: 100 nF] ← decouples AC from bias rail
```

With the bias at 2.5V, the output rests at ~2.5V (ADC ≈ 512) and swings up and down with each breath cycle. The full 0–5V ADC range is used.

### Complete A0 Signal Chain Summary

```
Piezo → [1 MΩ + 1 µF HP @ 0.16 Hz] → [LM358A voltage follower]
      → [× −45.5 gain] → [2.5V DC bias] → [LP @ 338 Hz] → A0
```

---

## Channel A1 — Thermistor Voltage Divider

No op-amp needed. The thermistor forms the top leg of a resistive divider:

```
5V ──── [R_ref: 10 kΩ] ──┬──── [R_filter: 1 kΩ] ──── A1
                          │                             │
                     [NTC: 10 kΩ @ 25°C]           [C_f: 100 nF]
                          │                             │
                         GND ─────────────────────────GND
```

**Voltage divider output:** V = 5V × R_NTC / (R_ref + R_NTC)

At 25°C (room temp), R_NTC = R_ref = 10 kΩ → V = **2.5V** (ADC ≈ 512)  
During exhale (~34°C), R_NTC ≈ 7.3 kΩ → V = 5 × 7.3/17.3 = **2.11V** (ADC ≈ 433)  
During inhale (~20°C), R_NTC ≈ 13.4 kΩ → V = 5 × 13.4/23.4 = **2.86V** (ADC ≈ 586)

**ADC swing per breath cycle:** ~153 counts — easily detectable.

**RC low-pass filter** (R_filter and C_f): f_LP = 1/(2π × 1kΩ × 100nF) = **1.59 Hz**. Passes the full breathing rate range (up to 1 Hz) while rejecting electrical noise.

---

## Protection

**Polyfuse:** Place a 200 mA resettable polyfuse in series with the 5V line to the breadboard positive rail. This protects the PC's USB port from a short circuit. Required — demonstrators will verify it is present.

**ADC input protection:** The LM358 output cannot swing beyond its supply rails (0–5V) so the ADC input is inherently protected. No additional clamping diodes are needed, provided the LM358 is powered from the same 5V rail.

---

## Complete Component List for Core Analogue Circuit

| Component | Value | Qty | Source |
|---|---|---|---|
| LM358 dual op-amp DIP-8 | LM358P | 1 | Lab stock |
| Resistor | 1 MΩ | 2 | Lab stock |
| Resistor | 100 kΩ | 1 | Lab stock |
| Resistor | 10 kΩ | 3 | Lab stock |
| Resistor | 4.7 kΩ | 1 | Lab stock |
| Resistor | 2.2 kΩ | 1 | Lab stock |
| Resistor | 1 kΩ | 1 | Lab stock |
| Ceramic capacitor | 100 nF | 4 | Lab stock |
| Electrolytic capacitor | 1 µF | 1 | Lab stock |
| Polyfuse | 200 mA | 1 | Lab stock |

---

## Breadboard Layout Tips

1. Place the LM358 across the central divide — pins 1–4 one side, 5–8 the other
2. Fit the 100 nF decoupling cap immediately adjacent to VCC (pin 8) and GND (pin 4) — not at the far end of the rail
3. Route the piezo wires away from power rails and PWM digital outputs (D9–D11)
4. Keep signal wires short between op-amp output and Arduino ADC — long wires act as antennas and pick up 50 Hz mains
5. Connect all grounds to a single rail before anything else — a floating ground is the most common cause of unexplained noise

---

## Sanity Checks Before First Power-On

| Measurement | Expected value | If wrong |
|---|---|---|
| Voltage at breadboard +ve rail | 5.0V ± 0.1V | Check USB connection, polyfuse |
| LM358 pin 8 (VCC) | 5.0V | Check wiring to rail |
| LM358 pin 4 (GND) | 0V | Check GND connection |
| LM358 pin 5 (non-inv input) | 2.5V | Check bias divider |
| LM358 pin 7 (output, no piezo) | ~2.5V | Op-amp saturated — check feedback |
| Arduino A0 (no movement) | ~512 in Serial Monitor | Bias not reaching ADC |
| Arduino A1 (room temp) | ~540–560 | Check thermistor divider wiring |

---

## Optional Extension — PPG Transimpedance Amplifier (BPW34 + TSAL6400)

If adding the DIY PPG extension (Option A from the components doc), a third analogue channel uses a second LM358 IC as a transimpedance amplifier (TIA).

**Parts required** (see doc 01 for Onecall order codes):
- **Vishay BPW34** silicon PIN photodiode — CPC code SC07697 or Farnell code 1045425
- **Vishay TSAL6400** 940nm IR LED — Farnell code 1652530

```
TSAL6400 anode ──── [R_LED: 150 Ω] ──── D3 (PWM at ~880 Hz for modulation)
TSAL6400 cathode ──── GND

BPW34 anode ──── (−) input of second LM358
BPW34 cathode ──── 5V
[Rf_TIA: 1 MΩ] between (−) input and output
(+) input ──── 2.5V bias (from existing divider)
Output ──── [R_LP: 10 kΩ] ──── A2
                                │
                          [C_LP: 100 nF]
                                │
                               GND
```

**How it works:** The TSAL6400 illuminates the fingertip. The BPW34 detects the reflected IR light — the amount varies with blood volume in the capillaries on each heartbeat, giving the PPG waveform. The transimpedance amplifier converts the BPW34's photocurrent to a voltage: V = I_photo × Rf_TIA. With Rf = 1 MΩ and typical photocurrents of 1–10 µA the output swing is 1–10V — start with Rf = 100 kΩ and increase if the signal is too small.

**TSAL6400 electrical notes:** forward voltage ~1.35V, so the 150 Ω resistor limits current to (5 − 1.35) / 150 ≈ 24 mA — within the safe operating range. Modulating the LED at ~880 Hz and demodulating in firmware or Python (lock-in style) improves rejection of ambient light noise.