/*
 * Arduino Uno R3 — Real-time guitar DSP pipeline (Clipping -> Biquad EQ -> Delay)
 *
 * Implements the firmware DSP pipeline from the SF4 Interim Report (Fig. 1 & 3):
 *
 *     ADC --> [ Stage 1: Clipping ] --> [ Stage 2: Biquad IIR EQ ] --> [ Stage 3: Delay ] --> PWM DAC
 *                       ^                          ^                          ^
 *                  clip threshold            b0..b2, a1, a2             length / feedback / mix
 *                                       (all held in the Parameter Bank)
 *
 * SIGNAL PATH / TIMING
 *   INPUT : guitar biased to 2.5 V on A0; ADC -> 0..1023 (512 = silence).
 *   OUTPUT: pin 10 (OC1B), 8-bit Fast PWM, 62.5 kHz carrier -> RC reconstruction.
 *   OPTO  : pin 9  (OC1A), same timer, sets analog VGA gain via the optocoupler.
 *
 *   Timer1 generates the 62.5 kHz PWM carrier purely in hardware (no overflow
 *   ISR). The DSP runs in the ADC-complete interrupt, which fires once per
 *   conversion at the free-running ADC rate FS ~= 19.23 kHz (the sample rate,
 *   matching the report's ~20 kHz plan). OCR1B is double-buffered, so writing it
 *   at 19.23 kHz while the carrier runs at 62.5 kHz is glitch-free. Driving the
 *   tick from ADC_vect (not the 62.5 kHz overflow) avoids ~2/3 wasted ISR
 *   entries, freeing CPU for serial and heavier effects.
 *
 * FIXED-POINT
 *   No float inside the ISR (no FPU on AVR). The biquad uses Q13 coefficients:
 *   1.0 == (1 << 13) == 8192. Accumulate in int32, then >> 13. Audio samples are
 *   carried as signed values centred on 0 (range -512..+511).
 *
 * PARAMETER BANK
 *   All effect parameters live in one `volatile Params` struct so the host (over
 *   serial/UART) can update them live. Multi-byte writes are wrapped in cli/sei so
 *   the ISR never reads a half-updated coefficient. Defaults = clean passthrough.
 */

#include <math.h>
#include <stdlib.h>   // atol
#include <stdio.h>    // sscanf

// ---------------- Fixed-point + buffer configuration ----------------
#define Q_SHIFT      13            // biquad coefficient fractional bits (Q13)
#define Q_ONE        (1L << Q_SHIFT)   // 1.0 in Q13 == 8192
#define SAMPLE_MIN  (-512)         // signed sample range (10-bit, centred at 0)
#define SAMPLE_MAX   (511)
#define MAX_DELAY    512           // delay-line taps; 512 * int16 = 1024 B SRAM
                                   // 512 / 19230 Hz ~= 26.6 ms max echo
static const float FS = 19230.0f;  // effective DSP sample rate (free-running ADC)

// ---------------- Parameter Bank (host-controlled) ----------------
struct Params {
  // Stage 1 — Clipping (hard clip / overdrive). Sample is clamped to +/-clipThresh.
  int16_t  clipThresh;   // 1..512; 512 = no clipping (clean)

  // Stage 2 — Biquad IIR EQ, Direct Form I, Q13 coefficients.
  // y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2   (denominator normalised, a0 = 1)
  int16_t  b0, b1, b2;
  int16_t  a1, a2;

  // Stage 3 — Delay (feedback comb / echo).
  uint16_t delaySamples; // 0 = off, else 1..MAX_DELAY
  uint8_t  feedback;     // 0..255 regeneration (Q8: 255 ~= 1.0)
  uint8_t  mix;          // 0..255 wet level    (Q8: 255 ~= 1.0)
};

// Default = transparent passthrough: no clip, flat EQ (b0 = 1.0), delay off.
volatile Params P = {
  /*clipThresh*/ 512,
  /*b0,b1,b2 */  (int16_t)Q_ONE, 0, 0,
  /*a1,a2    */  0, 0,
  /*delay    */  0, 0, 0
};

// ---------------- DSP state (ISR-private) ----------------
static int16_t  bq_x1, bq_x2, bq_y1, bq_y2;   // biquad delay registers
static int16_t  delayBuf[MAX_DELAY];          // echo line
static uint16_t delayIdx;                      // circular write/read cursor

// ---------------- Telemetry (MCU -> PC status) ----------------
// Written by the ISR, read+reset by loop() under cli/sei.
volatile int16_t g_peak     = 0;               // max |input| since last report
volatile uint8_t g_clipFlag = 0;               // 1 if clipping occurred since last report
uint16_t         g_rxErrors = 0;               // malformed command frames (loop-side)

// ====================================================================
//  processEffect — runs the full pipeline on one 10-bit sample.
//  in : 0..1023 (512 = silence)   ->   out : 0..1023
// ====================================================================
uint16_t processEffect(uint16_t in) {
  int16_t x = (int16_t)in - 512;            // -> signed, centred at 0

  // ---- Telemetry: track input level (VU / "buffer health") ------------------
  int16_t ax = (x < 0) ? -x : x;
  if (ax > g_peak) g_peak = ax;

  // ---- Stage 1: Clipping ----------------------------------------------------
  // Hard symmetric clip. A lower threshold squares off the peaks => overdrive.
  int16_t t = P.clipThresh;
  if (x >  t) { x =  t; g_clipFlag = 1; }
  if (x < -t) { x = -t; g_clipFlag = 1; }

  // ---- Stage 2: Biquad IIR EQ (Direct Form I, Q13) --------------------------
  int32_t acc = (int32_t)P.b0 * x
              + (int32_t)P.b1 * bq_x1
              + (int32_t)P.b2 * bq_x2
              - (int32_t)P.a1 * bq_y1
              - (int32_t)P.a2 * bq_y2;
  int16_t y = (int16_t)(acc >> Q_SHIFT);

  bq_x2 = bq_x1; bq_x1 = x;                  // shift input history
  bq_y2 = bq_y1; bq_y1 = y;                  // shift output history

  // ---- Stage 3: Delay (feedback comb) ---------------------------------------
  uint16_t dlen = P.delaySamples;
  if (dlen) {
    if (dlen > MAX_DELAY) dlen = MAX_DELAY;
    if (delayIdx >= dlen) delayIdx = 0;

    int16_t echo = delayBuf[delayIdx];                 // oldest stored sample
    // Wet output = dry + mix * echo
    int32_t outv  = (int32_t)y + (((int32_t)echo * P.mix) >> 8);
    // Stored sample = dry + feedback * echo  (regenerates the echo tail)
    int32_t store = (int32_t)y + (((int32_t)echo * P.feedback) >> 8);

    if (store > SAMPLE_MAX) store = SAMPLE_MAX;         // keep line bounded
    if (store < SAMPLE_MIN) store = SAMPLE_MIN;
    delayBuf[delayIdx] = (int16_t)store;

    if (++delayIdx >= dlen) delayIdx = 0;               // advance cursor
    y = (int16_t)outv;
  }

  // ---- Back to 10-bit unsigned, clamped ------------------------------------
  int16_t out = y + 512;
  if (out > 1023) out = 1023;
  if (out < 0)    out = 0;
  return (uint16_t)out;
}

// ====================================================================
//  Parameter-bank helpers (called from loop(), never the ISR)
// ====================================================================

// Read the live (volatile) bank into a plain struct we can edit safely.
// Field-wise because a volatile struct can't use the implicit copy ctor.
Params snapshotParams() {
  Params np;
  noInterrupts();
  np.clipThresh   = P.clipThresh;
  np.b0 = P.b0; np.b1 = P.b1; np.b2 = P.b2;
  np.a1 = P.a1; np.a2 = P.a2;
  np.delaySamples = P.delaySamples;
  np.feedback     = P.feedback;
  np.mix          = P.mix;
  interrupts();
  return np;
}

// Atomically copy an edited struct back into the live bank (field-wise).
void setParams(const Params &np) {
  int16_t clip = np.clipThresh;
  if (clip < 1)   clip = 1;
  if (clip > 512) clip = 512;
  uint16_t dl = np.delaySamples;
  if (dl > MAX_DELAY) dl = MAX_DELAY;
  noInterrupts();
  P.clipThresh   = clip;
  P.b0 = np.b0; P.b1 = np.b1; P.b2 = np.b2;
  P.a1 = np.a1; P.a2 = np.a2;
  P.delaySamples = dl;
  P.feedback     = np.feedback;
  P.mix          = np.mix;
  interrupts();
}

// Compute RBJ-cookbook biquad coefficients (float, OK outside the ISR) and
// load them as Q13 ints. type: 0 = flat, 1 = low-pass, 2 = high-pass.
void setBiquad(uint8_t type, float fc, float Q) {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // flat default
  if (type != 0) {
    float w0    = 2.0f * PI * fc / FS;
    float cosw  = cos(w0);
    float alpha = sin(w0) / (2.0f * Q);
    float a0    = 1.0f + alpha;
    if (type == 1) {                 // low-pass
      b0 = (1 - cosw) * 0.5f; b1 = 1 - cosw; b2 = b0;
    } else {                         // high-pass
      b0 = (1 + cosw) * 0.5f; b1 = -(1 + cosw); b2 = b0;
    }
    a1 = -2.0f * cosw; a2 = 1.0f - alpha;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;   // normalise (a0 = 1)
  }
  noInterrupts();
  P.b0 = (int16_t)lroundf(b0 * Q_ONE);
  P.b1 = (int16_t)lroundf(b1 * Q_ONE);
  P.b2 = (int16_t)lroundf(b2 * Q_ONE);
  P.a1 = (int16_t)lroundf(a1 * Q_ONE);
  P.a2 = (int16_t)lroundf(a2 * Q_ONE);
  interrupts();
}

// ====================================================================
//  Setup
// ====================================================================
void setup() {
  pinMode(A0, INPUT);    // audio input
  pinMode(10, OUTPUT);   // audio PWM out (OC1B)
  pinMode(9,  OUTPUT);   // optocoupler PWM out (OC1A)

  Serial.begin(115200);
  Serial.println(F("--- SF4 DSP Pipeline: Clip -> Biquad EQ -> Delay ---"));
  Serial.println(F("Commands (one per line):"));
  Serial.println(F("  g<0..255>          optocoupler/VGA gain (OCR1A)"));
  Serial.println(F("  c<1..512>          clip threshold (512=clean)"));
  Serial.println(F("  d<0..512>          delay length in samples (0=off)"));
  Serial.println(F("  f<0..255>          delay feedback   m<0..255> delay mix"));
  Serial.println(F("  l<fc>              low-pass EQ @ fc Hz (Q=0.707)"));
  Serial.println(F("  h<fc>              high-pass EQ @ fc Hz (Q=0.707)"));
  Serial.println(F("  B b0 b1 b2 a1 a2   raw Q13 biquad coeffs (1.0=8192)"));
  Serial.println(F("  x                  bypass (clean passthrough)"));
  Serial.println(F("  S,clip,b0,b1,b2,a1,a2,delay,fb,mix,gain   set-all frame"));
  Serial.println(F("Telemetry out @10Hz: T,clip,gain,peak,rxErr"));
  Serial.println(F("----------------------------------------------------"));

  // ---- ADC: free-running so a fresh sample is always ready to read ----
  ADMUX  = (1 << REFS0);                  // AVcc (5 V) ref, right-adjusted, A0
  ADCSRB = 0;                             // free-running trigger source
  DIDR0  = (1 << ADC0D);                  // disable digital input buffer on A0
  ADCSRA = (1 << ADEN)                    // enable ADC
         | (1 << ADATE)                   // auto-trigger (free-running)
         | (1 << ADIE)                    // conversion-complete interrupt = sample tick
         | (1 << ADPS2) | (1 << ADPS1)    // /64 -> 250 kHz ADC clk (~19 kHz/sample)
         | (1 << ADSC);                   // start first conversion

  // ---- Timer1: Mode 5 = 8-bit Fast PWM (TOP = 0x00FF) on OC1A & OC1B ----
  TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM10);
  TCCR1B = (1 << WGM12)  | (1 << CS10);   // no prescale -> 62500 Hz
  OCR1B  = 128;                           // audio mid-scale (silence)
  OCR1A  = 0;                             // optocoupler LED off
  TIMSK1 = 0;                             // no timer ISR; PWM carrier is pure HW.
                                          // The sample tick is ADC_vect (~19 kHz).
}

// ====================================================================
//  Main loop — non-blocking host command parser
//  (kept out of the audio path; never stalls the ISR)
// ====================================================================
// Act on one fully-received command line: cmd letter + an argument string.
void handleCommand(char cmd, const char *arg) {
  switch (cmd) {
    case 'g': OCR1A = constrain(atol(arg), 0, 255);
              Serial.print(F(">> VGA gain  = ")); Serial.println(OCR1A); break;

    case 'c': { Params np = snapshotParams(); np.clipThresh = constrain(atol(arg), 1, 512);
                setParams(np);
                Serial.print(F(">> clip thr  = ")); Serial.println(np.clipThresh); } break;

    case 'd': { Params np = snapshotParams(); np.delaySamples = constrain(atol(arg), 0, MAX_DELAY);
                setParams(np); delayIdx = 0;
                Serial.print(F(">> delay len = ")); Serial.println(np.delaySamples); } break;

    case 'f': { Params np = snapshotParams(); np.feedback = constrain(atol(arg), 0, 255);
                setParams(np);
                Serial.print(F(">> feedback  = ")); Serial.println(np.feedback); } break;

    case 'm': { Params np = snapshotParams(); np.mix = constrain(atol(arg), 0, 255);
                setParams(np);
                Serial.print(F(">> mix       = ")); Serial.println(np.mix); } break;

    case 'l': { long fc = atol(arg); setBiquad(1, (float)fc, 0.707f);
                Serial.print(F(">> low-pass @ ")); Serial.print(fc); Serial.println(F(" Hz")); } break;

    case 'h': { long fc = atol(arg); setBiquad(2, (float)fc, 0.707f);
                Serial.print(F(">> high-pass @ ")); Serial.print(fc); Serial.println(F(" Hz")); } break;

    case 'B': { Params np = snapshotParams();
                int b0, b1, b2, a1, a2;
                if (sscanf(arg, "%d %d %d %d %d", &b0, &b1, &b2, &a1, &a2) == 5) {
                  np.b0 = b0; np.b1 = b1; np.b2 = b2; np.a1 = a1; np.a2 = a2;
                  setParams(np); Serial.println(F(">> raw biquad loaded"));
                } else {
                  Serial.println(F(">> B needs 5 ints: B b0 b1 b2 a1 a2"));
                }
              } break;

    // Atomic "set all" frame from the host translation layer:
    //   S,<clip>,<b0>,<b1>,<b2>,<a1>,<a2>,<delay>,<fb>,<mix>,<gain>
    case 'S': { Params np;
                int clip, b0, b1, b2, a1, a2, dl, fb, mx, gn;
                if (sscanf(arg, ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                           &clip, &b0, &b1, &b2, &a1, &a2, &dl, &fb, &mx, &gn) == 10) {
                  np.clipThresh = clip; np.b0 = b0; np.b1 = b1; np.b2 = b2;
                  np.a1 = a1; np.a2 = a2;
                  np.delaySamples = dl; np.feedback = fb; np.mix = mx;
                  setParams(np); delayIdx = 0;
                  OCR1A = constrain(gn, 0, 255);
                  Serial.println(F(">> S applied"));
                } else {
                  g_rxErrors++;
                  Serial.println(F(">> S parse error"));
                }
              } break;

    case 'x': { Params np = { 512, (int16_t)Q_ONE, 0, 0, 0, 0, 0, 0, 0 };
                setParams(np); Serial.println(F(">> bypass (clean)")); } break;

    default: break;   // ignore blank lines / unknown commands
  }
}

// Emit one MCU->PC status frame: T,<clip>,<gain>,<peak>,<rxErrors>
void sendTelemetry() {
  uint8_t clip; int16_t peak;
  noInterrupts();                            // atomic read+reset of ISR-written state
  clip = g_clipFlag; g_clipFlag = 0;
  peak = g_peak;     g_peak     = 0;
  interrupts();

  Serial.print(F("T,"));
  Serial.print(clip);          Serial.print(',');
  Serial.print(OCR1A);         Serial.print(',');   // current gain
  Serial.print(peak);          Serial.print(',');
  Serial.println(g_rxErrors);
}

void loop() {
  // Accumulate a whole line, then parse it once. Robust to \n, \r\n, or no
  // line ending (parses on buffer-full) — unlike Serial.parseInt().
  // 80 chars holds the longest S-frame (signed Q13 coeffs + commas).
  static char line[80];
  static uint8_t len = 0;

  while (Serial.available() > 0) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {          // end of line -> dispatch
      if (len > 0) {
        line[len] = '\0';
        char *p = line;
        while (*p == ' ') p++;               // skip leading spaces
        char cmd = *p;
        if (cmd) {
          p++;
          while (*p == ' ') p++;             // skip spaces before the argument
          handleCommand(cmd, p);
        }
      }
      len = 0;                                // reset for next line
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;                       // buffer the character
    }
    // (overlong lines are silently truncated; commands here are short)
  }

  // ---- Periodic telemetry (~10 Hz) ----
  static uint32_t lastTelem = 0;
  uint32_t now = millis();
  if (now - lastTelem >= 100) {
    lastTelem = now;
    sendTelemetry();
  }
}

// ====================================================================
//  Audio ISR — fires once per ADC conversion (~19.23 kHz, the sample rate).
//  Reading ADC clears ADIF and the free-running ADC restarts the next
//  conversion, so no flag handling is needed.
// ====================================================================
ISR(ADC_vect) {
  uint16_t sample = ADC;             // 0..1023 (512 = silence); conversion done
  sample = processEffect(sample);    // clip -> biquad EQ -> delay
  OCR1B  = (uint8_t)(sample >> 2);   // 10-bit -> 8-bit PWM duty (double-buffered,
                                     // latches at next carrier BOTTOM -> glitch-free)
}
