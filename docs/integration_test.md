# System Integration and Testing

## Build Order

Build and test one subsystem at a time. Never connect an untested part to working circuitry — you will not know which part caused a failure.

```
Step 1:  Power rails and polyfuse
Step 2:  Thermistor voltage divider
Step 3:  Op-amp bias circuit (no piezo)
Step 4:  Op-amp with piezo connected
Step 5:  Full Arduino firmware — serial output only
Step 6:  LED outputs (RGB, pacer)
Step 7:  Buzzer
Step 8:  LCD
Step 9:  Python serial receive + data parsing
Step 10: ML classifier integration
Step 11: LLM integration
Step 12: Full system demonstration rehearsal
Step 13: (Optional) SpO2 extension
```

---

## Step 1 — Power Rails

Before placing any components:

1. Connect Arduino 5V → breadboard positive rail, GND → negative rail
2. Measure with multimeter: **5.0V ± 0.1V** across rails
3. Wire polyfuse in series with 5V: verify continuity through it (< 5 Ω)
4. Place 100 nF decoupling cap across rails at the centre of the board

**Pass:** 5V present, polyfuse shows continuity.

---

## Step 2 — Thermistor Channel

Build the voltage divider and RC filter; connect to A1. Upload:

```cpp
void setup() { Serial.begin(115200); }
void loop()  { Serial.println(analogRead(A1)); delay(100); }
```

Open Serial Monitor at 115200 baud.

| Condition | Expected ADC | Reason |
|---|---|---|
| Room temp ~21°C | 510–530 | NTC slightly above 10 kΩ at 21°C |
| Hold thermistor (finger, ~36°C) | ~400 | NTC resistance drops, divider output drops |
| Blow cold air on thermistor | ~560–580 | NTC resistance rises |

**Pass:** ADC around 520 at rest; measurably decreases when warmed, increases when cooled.

---

## Step 3 — Op-Amp Bias (No Piezo)

1. Build the voltage divider for 2.5V bias (two 10 kΩ resistors, 5V to GND)
2. Wire LM358A as a voltage follower (pin 2 → pin 1, pin 3 → bias)
3. Build the Stage 2 inverting amplifier with 2.5V on pin 5
4. Measure at each op-amp output pin with multimeter

| Point | Expected voltage |
|---|---|
| Bias node | 2.5V ± 0.05V |
| LM358A pin 1 (follower out) | 2.5V ± 0.1V |
| LM358B pin 7 (amp out) | 2.5V ± 0.3V |

**Pass:** Both op-amp outputs rest near 2.5V.

If an output is stuck at 0V or 5V (saturation):
- Check LM358 pin 8 = 5V, pin 4 = 0V
- Verify feedback path: pin 1 → pin 2 for Stage 1
- Check decoupling cap is placed correctly
- Confirm LM358, **not** TL072

---

## Step 4 — Piezo Sensor

1. Connect LDT1-028K wires to op-amp input
2. Tape sensor against chest with elastic strap — secure but not tight
3. Monitor A0 in Serial Monitor (simple `Serial.println(analogRead(A0))` sketch)

**Good signal looks like:**
```
501, 508, 519, 534, 548, 558, 556, 545, 529, 511, 497, 487, 484, 489, 499, 510, ...
```
A smooth, slow wave oscillating around 512 with amplitude 20–80 counts.

**Troubleshooting:**

| Symptom | Cause | Fix |
|---|---|---|
| Flat line at ~512 | Piezo not connected or gain stage wiring error | Recheck op-amp feedback |
| Saturated at 0 or 1023 | Gain too high or bias not working | Reduce Rf or recheck bias divider |
| 50 Hz noise visible | Long signal wires acting as antenna | Shorten wires, twist piezo leads |
| Signal only with firm pressure | Elastic strap too loose | Tighten strap; sensor must flex with breathing |
| Very small signal amplitude (<10 counts) | Gain stage not wired | Verify R3, Rf connected correctly |

---

## Step 5 — Full Firmware Serial Output

Flash the complete firmware. With no Python running, open Serial Monitor at 115200:

1. `DATA,...` packets appear every ~2.56 seconds ✓
2. Breathing rate changes when you breathe faster or slower ✓
3. Sending `CMD:SEV:2` in the Serial Monitor input field turns the RGB LED red ✓
4. Sending `CMD:BF:6` starts the pacer LED pulsing ✓

---

## Step 6–8 — Outputs

Test each output from Serial Monitor before connecting Python:

```
CMD:SEV:0     → Green LED
CMD:SEV:1     → Amber LED
CMD:SEV:2     → Red LED
CMD:SEV:3     → Flashing red

CMD:BF:6      → Pacer: one cycle per 10 seconds (slow, clear to see)
CMD:BF:20     → Pacer: approximately one cycle per 3 seconds
CMD:BF:0      → Pacer off

CMD:ALERT:1   → Buzzer: three-beep apnea pattern
CMD:ALERT:0   → Buzzer off
```

---

## Step 9 — Python Serial Integration

```bash
python collect_training_data.py normal
```

Verify the CSV file is written and contains sensible values (breathing rate ~0.2–0.3 Hz at rest).

---

## Step 10–11 — ML and LLM

1. Collect training data for all 6 classes (see doc 06)
2. Run `python train_classifier.py` — should complete in < 30 seconds
3. Test LLM independently before the full GUI:

```python
# Quick test in a Python shell:
from llm_client import LLMClient
from feature_extractor import RespiratoryFeatures
import time

client = LLMClient()
feat = RespiratoryFeatures(0.23, 1200, 0.75, 0.55, 0, time.time())
cls  = {'class': 'normal', 'confidence': 0.88,
        'probabilities': {'normal': 0.88, 'irregular': 0.12}}
resp = client.interpret(feat, cls, "EXERCISE", 5.0)
print(resp)
```

Expected: a valid `LLMResponse` with severity=0 and sensible commentary.

---

## Step 12 — Full System Demonstration Rehearsal

Run through the full demo scenario:

1. Start Python GUI, select EXERCISE mode, click Start
2. **Quiet breathing (1–2 min):** confirm class = "normal", green LED, sensible LLM commentary
3. **Exercise (2 min, e.g. walking or jumping jacks):** confirm breathing rate increases, class transitions to exercise pattern, amber/red LED if appropriate, LLM commentary updates
4. **Recovery (sit down):** confirm rate tracks downward, LLM suggests biofeedback rate, pacer LED activates
5. **Simulated apnea (hold breath ~12 seconds):** confirm buzzer sounds, GUI shows APNEA alert, severity flashes red
6. **Stop session, click Export:** confirm session summary file is written

---

## Debugging Reference Table

| Symptom | Likely cause | Fix |
|---|---|---|
| Serial Monitor shows nothing | Baud rate wrong | Set Serial Monitor to 115200 |
| Python cannot connect | Another program has port | Close Arduino Serial Monitor first |
| DATA packets stop after a few | Timer1 conflicts with `delay()` | Remove all `delay()` from loop() |
| Breathing rate always 0.39 Hz | FFT DC not removed | Subtract 512 in ISR before storing to piezoBuffer |
| LLM always returns None | API key not set | `echo $ANTHROPIC_API_KEY` to check |
| RGB LED shows wrong colour | Common cathode wired to 5V | Flip LED — cathode (longest pin) to GND |
| Op-amp output saturated | TL072 used instead of LM358 | Replace IC — TL072 cannot run from 5V |
| LCD shows garbled characters | Wrong I2C address | Run I2C scanner sketch |
| Buzzer sounds continuously | alertMode stuck | Check `CMD:ALERT:0` is being sent |
| Classifier always predicts same class | Not enough training data | Collect more sessions for minority classes |

---

## Step 13 (Optional) — SpO2 Extension

Add only after Steps 1–12 are fully verified. See doc 01 for hardware, doc 03 for firmware. After adding:

1. Run I2C scanner — confirm you see two addresses (LCD + MAX30100/30102)
2. Load the MAX30100 example sketch, verify SpO2 and HR appear in Serial Monitor
3. Update `parse_data_packet()` to handle the extra fields
4. Retrain the classifier with the new features