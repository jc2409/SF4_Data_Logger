/*
 * Arduino Uno R3 — 8-bit PWM audio output (passthrough) + optocoupler control
 *
 * INPUT : guitar signal biased to 2.5 V, swinging 0–5 V  -> A0
 *         ADC reads 0..1023, where 512 ≈ 2.5 V = silence.
 *
 * OUTPUTS (both on Timer1, 8-bit Fast PWM, carrier = 62500 Hz):
 *   AUDIO       -> pin 10 (OC1B) -> low-pass filter -> audio out
 *   OPTOCOUPLER -> pin 9  (OC1A) -> RC -> DC -> LED in the VGA gain stage
 *
 * WHY 62.5 kHz (8-bit) AND NOT 15.6 kHz (10-bit)?
 *   Guitar tone needs audio out to ~5 kHz (harmonics). At a 15.6 kHz carrier
 *   the carrier is only ~3x above the audio, so NO filter can pass 5 kHz while
 *   rejecting the carrier — you're forced to choose ripple OR dull tone.
 *   Pushing the carrier to 62.5 kHz puts it ~12x above the audio, so a gentle
 *   filter at fc ≈ 5 kHz covers the full guitar range AND kills the carrier.
 *   Trade-off: 8-bit = 256 levels instead of 1024 (≈48 dB SNR, fine for guitar).
 *
 * RECOMMENDED ANALOG OUTPUT CHAIN (matches the build notes):
 *   LOW-PASS (sets top of band, kills carrier) — two passive RC + LM358 buffer:
 *     pin 10 ─R1(2.2k)─┬─→(+)LM358 buf─out─R2(2.2k)─┬─→ C8
 *                     C1(15nF)                      C2(15nF)
 *                      │                             │
 *                     GND                           GND     (each fc ≈ 4.8 kHz)
 *   HIGH-PASS (sets bottom of band, strips 2.5 V bias) — C8 + R13:
 *     ...─┬─ C8(10uF) ─┬─→ audio jack
 *         │           R13(100k)
 *         │            │
 *        node B       GND                           (fc ≈ 0.16 Hz, no bass loss)
 *   NOTE: do NOT use a Sallen-Key around the LM358 — the LM358 is too slow
 *         (~1 MHz GBW) and the topology leaks the carrier through Ca. Use the
 *         passive RC + buffer chain above instead.
 *
 * KEY POINT: OC1A = pin 9, OC1B = pin 10. They are two INDEPENDENT compare
 *            outputs of the same timer, so audio (OCR1B) and the optocoupler
 *            (OCR1A) coexist without fighting. Both run the full 0..255 range
 *            — do NOT use analogWrite() here (it assumes the core's default
 *            8-bit timer config, which we have replaced).
 *
 * TIMING: Timer1 overflow ISR = sample tick = 16 MHz / 256 = 62500 Hz.
 *         (Effective audio sample rate is set by the free-running ADC at
 *          ~19 kHz; the PWM just refreshes faster than that, which is fine.)
 */

void setup() {
  pinMode(A0, INPUT);   // audio input
  pinMode(10, OUTPUT);  // audio PWM out (OC1B)
  pinMode(9,  OUTPUT);  // optocoupler PWM out (OC1A)

  Serial.begin(9600);
  Serial.println("--- Optocoupler Interactive Terminal ---");
  Serial.println("Type a gain value 0..255 and hit Enter.");
  Serial.println("0   = LED Off (Max LDR resistance)");
  Serial.println("255 = LED Max (Min LDR resistance)");
  Serial.println("----------------------------------------");

  // ---- ADC: free-running so a fresh sample is always ready to read ----
  ADMUX  = (1 << REFS0);                  // AVcc (5 V) ref, right-adjusted, channel A0
  ADCSRB = 0;                             // free-running trigger source
  DIDR0  = (1 << ADC0D);                  // disable digital input buffer on A0 (less noise)
  ADCSRA = (1 << ADEN)                    // enable ADC
         | (1 << ADATE)                   // auto-trigger (free-running)
         | (1 << ADPS2) | (1 << ADPS1)    // prescaler /64 -> 250 kHz ADC clock (~19 kHz/sample)
         | (1 << ADSC);                   // start first conversion

  // ---- Timer1: Mode 5 = 8-bit Fast PWM (TOP = 0x00FF) on BOTH OC1A & OC1B ----
  TCCR1A = (1 << COM1A1)                  // non-inverting PWM on OC1A (pin 9, optocoupler)
         | (1 << COM1B1)                  // non-inverting PWM on OC1B (pin 10, audio)
         | (1 << WGM10);                  // WGM[11:10] = 01
  TCCR1B = (1 << WGM12)                   // WGM12 = 1  -> mode 5 (WGM[13:12] = 01)
         | (1 << CS10);                   // no prescale -> 16 MHz / 256 = 62500 Hz

  OCR1B  = 128;                           // audio: start mid-scale (2.5 V = silence)
  OCR1A  = 0;                             // optocoupler: LED off to start
  TIMSK1 = (1 << TOIE1);                  // overflow interrupt = our sample tick
}

void loop() {
  // Optocoupler gain control over serial (non-blocking; never stalls audio)
  if (Serial.available() > 0) {
    int value = Serial.parseInt();
    while (Serial.available() > 0) Serial.read();   // flush newline

    OCR1A = constrain(value, 0, 255);               // set LED brightness (full 8-bit)

    Serial.print(">> Optocoupler PWM set to: ");
    Serial.println(OCR1A);
  }
}

// Fires once per PWM cycle = 62500 Hz
ISR(TIMER1_OVF_vect) {
  uint16_t sample = ADC;        // latest 0..1023 reading (512 = 2.5 V bias)

  // ---- DSP goes here. Passthrough for now: ----
  // sample = processEffect(sample);

  OCR1B = sample >> 2;          // scale 10-bit ADC -> 8-bit PWM (512 -> 128 = mid)
}
