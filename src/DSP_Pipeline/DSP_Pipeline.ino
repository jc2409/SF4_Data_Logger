/*
 * Arduino Uno R3 — Real-time guitar DSP pipeline (Clipping -> Biquad EQ -> Delay)
 * WITH 3-ZONE ZERO-LATENCY AUTO-VGA
 *
 * Implements the firmware DSP pipeline from the SF4 Interim Report (Fig. 1 & 3):
 *
 * ADC --> [ Stage 1: Clipping ] --> [ Stage 2: Biquad IIR EQ ] --> [ Stage 3: Delay ] --> PWM DAC
 * ^                          ^                          ^
 * clip threshold            b0..b2, a1, a2             length / feedback / mix
 * (all held in the Parameter Bank)
 *
 * SIGNAL PATH / TIMING
 * INPUT : guitar biased to 2.5 V on A0; ADC -> 0..1023 (512 = silence).
 * OUTPUT: pin 10 (OC1B), 8-bit Fast PWM, 62.5 kHz carrier -> RC reconstruction.
 * OPTO  : pin 9  (OC1A), same timer, sets analog VGA gain via the optocoupler.
 *
 * Timer1 generates the 62.5 kHz PWM carrier purely in hardware. The DSP runs 
 * in the ADC-complete interrupt, firing at the free-running ADC rate FS ~= 19.23 kHz. 
 * OCR1B is double-buffered, making updates glitch-free.
 *
 * FIXED-POINT
 * No float inside the ISR. The biquad uses Q13 coefficients: 1.0 == (1 << 13) == 8192.
 */

#include <math.h>
#include <stdlib.h>   // atol
#include <stdio.h>    // sscanf

// ---------------- Fixed-point + buffer configuration ----------------
#define Q_SHIFT      13                // biquad coefficient fractional bits (Q13)
#define Q_ONE        (1L << Q_SHIFT)   // 1.0 in Q13 == 8192
#define SAMPLE_MIN  (-512)             // signed sample range (10-bit, centred at 0)
#define SAMPLE_MAX   (511)
#define MAX_DELAY    512               // delay-line taps; 512 * int16 = 1024 B SRAM
static const float FS = 19230.0f;      // effective DSP sample rate

// ---------------- Parameter Bank (host-controlled) ----------------
struct Params {
  int16_t  clipThresh;   // 1..512; 512 = no clipping (clean)
  int16_t  b0, b1, b2;   // Biquad IIR EQ, Direct Form I, Q13 coefficients
  int16_t  a1, a2;
  uint16_t delaySamples; // 0 = off, else 1..MAX_DELAY
  uint8_t  feedback;     // 0..255 regeneration (Q8: 255 ~= 1.0)
  uint8_t  mix;          // 0..255 wet level    (Q8: 255 ~= 1.0)
};

// Default = transparent passthrough: no clip, flat EQ, delay off.
volatile Params P = {
  /*clipThresh*/ 512,
  /*b0,b1,b2 */  (int16_t)Q_ONE, 0, 0,
  /*a1,a2    */  0, 0,
  /*delay    */  0, 0, 0
};

// ---------------- DSP state (ISR-private) ----------------
static int16_t  bq_x1, bq_x2, bq_y1, bq_y2;   
static int16_t  delayBuf[MAX_DELAY];          
static uint16_t delayIdx;                     

// ---------------- Telemetry & Auto-VGA ----------------
volatile int16_t g_peak     = 0;               // max |input| since last AGC check
volatile uint8_t g_clipFlag = 0;               // 1 if clipping occurred
uint16_t         g_rxErrors = 0;               // malformed command frames 
bool             g_autoVGA  = false;           // Toggle for Automatic Gain Control

// ====================================================================
//  processEffect — runs the full pipeline on one 10-bit sample.
// ====================================================================
uint16_t processEffect(uint16_t in) {
  int16_t x = (int16_t)in - 512;            // -> signed, centred at 0

  // ---- Telemetry: track input level (VU / "buffer health") ----
  int16_t ax = (x < 0) ? -x : x;
  if (ax > g_peak) g_peak = ax;

  // ---- Stage 1: Clipping ----
  int16_t t = P.clipThresh;
  if (x >  t) { x =  t; g_clipFlag = 1; }
  if (x < -t) { x = -t; g_clipFlag = 1; }

  // ---- Stage 2: Biquad IIR EQ ----
  int32_t acc = (int32_t)P.b0 * x
              + (int32_t)P.b1 * bq_x1
              + (int32_t)P.b2 * bq_x2
              - (int32_t)P.a1 * bq_y1
              - (int32_t)P.a2 * bq_y2;
  int16_t y = (int16_t)(acc >> Q_SHIFT);

  bq_x2 = bq_x1; bq_x1 = x;                  
  bq_y2 = bq_y1; bq_y1 = y;                  

  // ---- Stage 3: Delay ----
  uint16_t dlen = P.delaySamples;
  if (dlen) {
    if (dlen > MAX_DELAY) dlen = MAX_DELAY;
    if (delayIdx >= dlen) delayIdx = 0;

    int16_t echo = delayBuf[delayIdx];                 
    int32_t outv  = (int32_t)y + (((int32_t)echo * P.mix) >> 8);
    int32_t store = (int32_t)y + (((int32_t)echo * P.feedback) >> 8);

    if (store > SAMPLE_MAX) store = SAMPLE_MAX;         
    if (store < SAMPLE_MIN) store = SAMPLE_MIN;
    delayBuf[delayIdx] = (int16_t)store;

    if (++delayIdx >= dlen) delayIdx = 0;               
    y = (int16_t)outv;
  }

  // ---- Back to 10-bit unsigned, clamped ----
  int16_t out = y + 512;
  if (out > 1023) out = 1023;
  if (out < 0)    out = 0;
  return (uint16_t)out;
}

// ====================================================================
//  Parameter-bank helpers (called from loop(), never the ISR)
// ====================================================================
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

void setBiquad(uint8_t type, float fc, float Q) {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   
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
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;   
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
  Serial.println(F("  v<0 or 1>          Toggle AUTO-VGA (0=off, 1=on)"));
  Serial.println(F("  g<0..255>          MANUAL optocoupler/VGA gain"));
  Serial.println(F("  c<1..512>          clip threshold (512=clean)"));
  Serial.println(F("  d<0..512>          delay length in samples (0=off)"));
  Serial.println(F("  f<0..255>          delay feedback   m<0..255> delay mix"));
  Serial.println(F("  l<fc>              low-pass EQ @ fc Hz (Q=0.707)"));
  Serial.println(F("  h<fc>              high-pass EQ @ fc Hz (Q=0.707)"));
  Serial.println(F("  x                  bypass (clean passthrough)"));
  Serial.println(F("Telemetry out @10Hz: T,clip,gain,peak,rxErr"));
  Serial.println(F("----------------------------------------------------"));

  // ---- ADC Initialization ----
  ADMUX  = (1 << REFS0);                  
  ADCSRB = 0;                             
  DIDR0  = (1 << ADC0D);                  
  ADCSRA = (1 << ADEN)                    
         | (1 << ADATE)                   
         | (1 << ADIE)                    
         | (1 << ADPS2) | (1 << ADPS1)    
         | (1 << ADSC);                   

  // ---- Timer1: Mode 5 (8-bit Fast PWM) ----
  TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM10);
  TCCR1B = (1 << WGM12)  | (1 << CS10);   
  OCR1B  = 128;                           
  OCR1A  = 0;                             
  TIMSK1 = 0;                             
}

// ====================================================================
//  Main loop — Command Parser & Dual-Timer Logic
// ====================================================================
void handleCommand(char cmd, const char *arg) {
  switch (cmd) {
    case 'v': g_autoVGA = (atol(arg) != 0);
              Serial.print(F(">> Auto-VGA = ")); Serial.println(g_autoVGA ? "ON" : "OFF"); break;

    case 'g': OCR1A = constrain(atol(arg), 0, 255);
              g_autoVGA = false; // Manual gain override disables Auto-VGA
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
                  Serial.println(F(">> B needs 5 ints"));
                }
              } break;

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

    default: break;   
  }
}

void loop() {
  static char line[80];
  static uint8_t len = 0;

  // --- Serial Command Parser ---
  while (Serial.available() > 0) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (len > 0) {
        line[len] = '\0';
        char *p = line;
        while (*p == ' ') p++;
        char cmd = *p;
        if (cmd) {
          p++;
          while (*p == ' ') p++;
          handleCommand(cmd, p);
        }
      }
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;
    }
  }

  // --- Timers for Multi-tasking ---
  static uint32_t lastAGC = 0;
  static uint32_t lastTelem = 0;
  static int16_t  telem_peak = 0; // Stores the max peak for the 100ms printout
  
  uint32_t now = millis();

  // ====================================================================
  // 1. FAST AGC LOOP (Runs every 10ms for near-zero latency)
  // ====================================================================
  if (now - lastAGC >= 10) {
    lastAGC = now;

    noInterrupts();
    int16_t currentPeak = g_peak;
    g_peak = 0; // Reset instantly so we track the *next* 10ms window
    interrupts();

    // Save the highest peak we see for the slow Serial monitor print
    if (currentPeak > telem_peak) telem_peak = currentPeak; 

    if (g_autoVGA) {
        int targetHigh = 450; // Extreme Large threshold
        int targetLow  = 40;  // Extreme Small threshold
        int defaultVGA = 15;  // The 10-20 resting "Sweet Spot"

        if (currentPeak > targetHigh) {
            // ZONE 1: INSTANT LIMITER (Fast Attack to prevent clipping)
            OCR1A = 50; 
        } 
        else if (currentPeak < targetLow && currentPeak > 5) {
            // ZONE 2: INSTANT BOOST (Fast Attack to save dying notes)
            OCR1A = 0;
        } 
        else {
            // ZONE 3: NORMAL RANGE (Slow Release to default)
            if (OCR1A > defaultVGA) OCR1A--;
            else if (OCR1A < defaultVGA) OCR1A++;
        }
    }
  }

  // ====================================================================
  // 2. SLOW TELEMETRY LOOP (Runs every 100ms for readability)
  // ====================================================================
  if (now - lastTelem >= 100) {
    lastTelem = now;

    uint8_t clip;
    noInterrupts();
    clip = g_clipFlag; g_clipFlag = 0;
    interrupts();

    // Print the telemetry frame
    Serial.print(F("T,"));
    Serial.print(clip);          Serial.print(',');
    Serial.print(OCR1A);         Serial.print(','); 
    Serial.print(telem_peak);    Serial.print(','); // Print the 100ms max
    Serial.println(g_rxErrors);

    telem_peak = 0; // Reset telemetry tracker for the next 100ms
  }
}

// ====================================================================
//  Audio ISR
// ====================================================================
ISR(ADC_vect) {
  uint16_t sample = ADC;
  sample = processEffect(sample);
  OCR1B  = (uint8_t)(sample >> 2);
}