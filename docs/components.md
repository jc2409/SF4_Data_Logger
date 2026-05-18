# Components and Budget

## Core System Components (Must Purchase)

---

### 1. Piezoelectric Film Sensor

**Manufacturer:** TE Connectivity  
**Manufacturer P/N:** LDT1-028K  
**Description:** PVDF piezoelectric film sensor with 12-inch pre-attached wire leads. The wire leads are essential — the LDT0-028K has only crimped contacts and cannot be connected to a breadboard directly. When flexed by chest expansion during breathing, the film generates a voltage proportional to bending strain. Worn inside an elastic chest belt.

> This part appears under two Farnell order codes. Both resolve to the same LDT1-028K sensor with wire leads. Use whichever is in stock.

**Primary Onecall / Farnell order code: 2930739**  
Manufacturer P/N on this listing: 11028414-00 (TE Connectivity alternate P/N for LDT1-028K)  
**URL:** https://uk.farnell.com/te-connectivity/11028414-00/piezoelectric-sensor-vibration/dp/2930739

**Alternate Onecall / Farnell order code: 2771907**  
Also maps to LDT1-028K in Farnell catalogues.  
**URL:** https://uk.farnell.com/sensor-solutions-te-connectivity/ldt1-028k/piezoelectric-sensor-voltage-1/dp/2771907

**If in doubt:** tell the technicians to search `LDT1-028K` on Onecall and use whichever order code (2930739 or 2771907) is currently in stock.

**Public Farnell price:** ~£6.33 ex VAT (1 unit)  
**Estimated Onecall price:** ~£5.13  
**Quantity:** 1

---

### 2. NTC Thermistor 10 kΩ — Nasal Airflow Channel

**Manufacturer:** Vishay  
**Manufacturer P/N:** 2322 640 63103  
**Description:** Radial-leaded NTC disc thermistor, 10 kΩ at 25°C, B-constant ~3977 K, 17 mm tinned copper leads, 5% tolerance. Placed near one nostril in a small foam holder. Exhaled air (~34°C) lowers resistance; inhaled ambient air (~20°C) raises it, producing a clear voltage oscillation at the breathing rate via a simple resistor voltage divider. No amplification needed.

**Onecall / CPC order code: SN35070**  
**URL:** https://cpc.farnell.com/vishay/2322-640-63103/thermistor-ntc-10k-5/dp/SN35070

**Public CPC price:** £0.28 ex VAT (1 unit), £0.24 for 10+  
**Estimated Onecall price:** ~£0.23  
**Quantity:** 2 (one spare — leads are fragile)

---

### 3. RGB LED 5mm Common Cathode

**Manufacturer:** Pro Signal  
**Manufacturer P/N:** PSG91924  
**Description:** Pack of 3 × 5mm diffused RGB LEDs, common cathode. Four pins per LED: red (Vf ≈ 2.1V, 50mA max), green (Vf ≈ 3.1V, 30mA max), blue (Vf ≈ 3.1V, 30mA max), and shared cathode (longest pin). Driven from Arduino PWM pins D9/D10/D11 via current-limiting resistors. Displays exercise intensity and alert state as colour: green / amber / red.

> Note: the CPC product page currently reads "price for: pack of 5" in the pricing label, but the product itself is still a pack of 3 LEDs as stated in the description. You receive 3 LEDs per pack.

**Onecall / CPC order code: SC19718**  
**URL:** https://cpc.farnell.com/pro-signal/psg91924/led-5mm-r-g-b-com-cath-diffused/dp/SC19718

**Public CPC price:** £1.75 ex VAT (pack of 3)  
**Estimated Onecall price:** ~£1.42  
**Quantity:** 1 pack

---

## Core Budget Summary

| # | Component | Manufacturer P/N | Onecall Code | Public Price | Est. Onecall Price |
|---|---|---|---|---|---|
| 1 | LDT1-028K piezo sensor | LDT1-028K | 2930739 (or 2771907) | £6.33 | ~£5.13 |
| 2 | NTC thermistor 10 kΩ × 2 | 2322 640 63103 | SN35070 | £0.56 | ~£0.46 |
| 3 | RGB LED pack of 3 | PSG91924 | SC19718 | £1.75 | ~£1.42 |
| **Total** | | | | **£8.64** | **~£7.01** |

Leaves approximately **£6–8 of the £15 budget** for the optional SpO2 extension and/or LCD.

---

## Lab Stock — Free (Confirm with Technicians Before Ordering)

If any of these are not in lab stock, add them to the Onecall order at that point.

| Component | Value / Part | Purpose |
|---|---|---|
| LM358 dual op-amp, DIP-8 | LM358P or LM358N | Op-amp front-end. **Must be LM358 — not TL072.** The TL072 requires ±3.5V minimum supply and will not work from the Arduino's 5V rail. |
| Resistors | 1 MΩ, 100 kΩ, 33 kΩ, 10 kΩ, 4.7 kΩ, 2.2 kΩ, 1 kΩ, 220 Ω, 150 Ω | Filter, gain, bias network, LED current-limiting |
| Ceramic capacitors | 100 nF × 4, 10 nF × 2 | Decoupling, filter poles |
| Electrolytic capacitors | 10 µF × 2, 1 µF × 1 | DC coupling, filter |
| Green LED 5mm | Any | Biofeedback pacer (D6) |
| Active piezo buzzer, 5V | Self-drive type | Apnea alert (D7) |
| Resettable polyfuse | 200 mA | USB port protection — required |
| Jumper wires M-M | Assorted lengths | Breadboard-to-Arduino connections |
| Breadboard | 830-point | Circuit construction |

---

## Optional Extension A — DIY PPG: BPW34 + TSAL6400 (~£1 total)

Add after the core system is verified working. Gives heart rate and a secondary PPG-derived breathing signal. Does **not** give SpO2 (that requires two LED wavelengths with a more complex front-end).

**Advantage over Option B:** you design the transimpedance amplifier from scratch, which is genuine analogue circuit work scoring additional marks on the analogue design criterion.

---

### BPW34 Silicon PIN Photodiode

**Manufacturer:** Vishay  
**Manufacturer P/N:** BPW34  
**Description:** Silicon PIN photodiode, 900nm peak sensitivity, 7.5mm² active area, ±65° half-angle, 2nA dark current. Used as the detector in the PPG front-end with a transimpedance amplifier (LM358).

> The previously listed order code 1497753 is incorrect/obsolete. Use one of the two verified codes below.

**Option 1 — CPC / Onecall:**  
Order code: **SC07697**  
URL: https://cpc.farnell.com/vishay/bpw34/photodiode/dp/SC07697

**Option 2 — Farnell UK / Onecall:**  
Order code: **1045425**  
URL: https://uk.farnell.com/vishay/bpw34/silicon-pin-diode-900nm-65deg/dp/1045425

**Quantity:** 1

---

### TSAL6400 940nm IR LED

**Manufacturer:** Vishay  
**Manufacturer P/N:** TSAL6400  
**Description:** 940nm peak wavelength IR emitter, 5mm T-1 3/4 package, ~1.35V forward voltage, high radiant intensity, optimised for pairing with IR photodiode receivers. Used to illuminate the finger in the PPG circuit.

**Onecall / Farnell order code: 1652530**  
**URL:** https://uk.farnell.com/vishay/tsal6400/infrared-emitter-940nm-t-1-3-4/dp/1652530

**Public Farnell price:** ~£0.25–0.35 ex VAT  
**Quantity:** 1

---

**Total cost for Option A: ~£0.70–0.90 on Onecall**

---

## Optional Extension B — GY-MAX30100 SpO2 Module (~£2–3)

Gives true SpO2, heart rate, and PPG waveform via I2C. Drop-in module with an existing Arduino library, lower circuit complexity than Option A but no additional analogue design marks.

**⚠ Not available on Onecall, Farnell, or CPC.** Purchase separately:

- **eBay UK:** search `GY-MAX30100 Arduino` — https://www.ebay.co.uk
- **Amazon UK:** search `MAX30100 sensor module Arduino` — https://www.amazon.co.uk

Typical price: **£2.00–£3.50 delivered** from UK sellers.

> Buy the **green PCB (GY-MAX30100)** variant. Avoid the purple RCWL-0530 board — it has a known hardware design error requiring a resistor modification before it will work.

**Arduino library:** `MAX30100lib` by OXullo Intersecans (Arduino Library Manager)  
**I2C address:** 0x57 | **Power:** 3.3V Arduino pin  
**Pins used:** A4 (SDA), A5 (SCL) — shared I2C bus with LCD

---

## Optional Addition — 16×2 LCD with I2C Backpack (~£2–4)

Displays live breathing rate and status text on the hardware unit without requiring the PC screen.

Search `LCD 1602 I2C` directly on https://onecall.farnell.com — multiple compatible modules are available. Confirm the I2C address (typically 0x27 or 0x3F) by running the I2C scanner sketch in doc 09 before writing any LCD firmware.

---

## Full Budget Scenarios

| Scenario | Est. Onecall (core) | Extension | LCD | Total |
|---|---|---|---|---|
| Core only | ~£7.01 | — | — | **~£7.01** |
| Core + Option A (DIY PPG) | ~£7.01 | ~£0.80 | — | **~£7.81** |
| Core + Option B (GY-MAX30100) | ~£7.01 | £2–3.50 (eBay) | — | **~£9–10.50** |
| Core + Option B + LCD | ~£7.01 | £2–3.50 | ~£3 | **~£12–13.50** |

All scenarios remain within the £15 budget.

---

## Summary for Lab Technicians (Order Form)

| Component | Manufacturer P/N | Onecall / CPC Code | Qty |
|---|---|---|---|
| Piezo film sensor with wire leads | LDT1-028K | **2930739** (alt: 2771907) | 1 |
| NTC thermistor 10 kΩ | 2322 640 63103 | **SN35070** | 2 |
| RGB LED 5mm common cathode, pack of 3 | PSG91924 | **SC19718** | 1 |
| *If adding PPG extension:* | | | |
| BPW34 photodiode | BPW34 | **SC07697** or **1045425** | 1 |
| 940nm IR LED | TSAL6400 | **1652530** | 1 |

---

## Ordering Procedure

1. Verify stock availability and lead times on each Onecall product page — avoid items showing "US stock" (multi-week lead times)
2. Download and check the datasheet for each component before submitting
3. Discuss the final list with a demonstrator — they must approve it
4. Fill in the EIETL component order form using the codes above
5. Submit to lab technicians — standard UK stock arrives in 1–2 working days
6. **Order as early as possible** — do not wait until the interim report deadline