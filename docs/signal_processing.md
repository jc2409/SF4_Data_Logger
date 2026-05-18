# 04 — Signal Processing

## Why FFT for Respiratory Analysis

A breathing waveform is quasi-periodic — it repeats at the breathing rate with some variation in period and amplitude. The FFT decomposes this into constituent frequencies and reveals:

- **Where energy concentrates** — the dominant frequency peak is the breathing rate
- **How regular breathing is** — a narrow, sharp peak means consistent breathing; a broad diffuse spectrum means irregular or variable pattern
- **Whether multiple components are present** — harmonics, motion artefacts, or the superimposed tremor that distinguishes certain breathing disorders

This information is inaccessible from the raw time-domain waveform without considerably more complex processing.

---

## Sampling Theory

**Sampling rate:** 100 Hz  
**Nyquist limit:** 50 Hz  
**Useful respiratory band:** 0.1–2.0 Hz

The 100 Hz rate is far above the Nyquist minimum for breathing (which would only require 4 Hz). This oversampling means the anti-aliasing filter can be gentle (338 Hz corner), and the waveform shape — particularly the inspiratory-to-expiratory ratio — is captured with high temporal resolution.

---

## FFT Window and Frequency Resolution

With N = 256 samples at fs = 100 Hz:

| Parameter | Value |
|---|---|
| Window duration | 256 / 100 = **2.56 seconds** |
| Frequency resolution | 100 / 256 = **0.39 Hz per bin** |
| Maximum frequency | 100 / 2 = **50 Hz** |
| Useful one-sided bins | 128 (bins 0–127) |

**Bin-to-breathing-rate mapping:**

| Bin | Frequency (Hz) | Breaths per minute |
|---|---|---|
| 0 | 0 Hz | DC — always removed |
| 1 | 0.39 Hz | 23 br/min |
| 2 | 0.78 Hz | 47 br/min |
| 3 | 1.17 Hz | 70 br/min |

Normal resting breathing (12–20 br/min = 0.20–0.33 Hz) falls between bins 0 and 1. The 0.39 Hz resolution cannot distinguish 12 from 20 br/min from the bin index alone. Use quadratic interpolation in Python for sub-bin precision:

```python
def refine_peak(spectrum, peak_bin, freq_resolution):
    if peak_bin <= 0 or peak_bin >= len(spectrum) - 1:
        return peak_bin * freq_resolution
    y0 = spectrum[peak_bin - 1]
    y1 = spectrum[peak_bin]
    y2 = spectrum[peak_bin + 1]
    delta = 0.5 * (y0 - y2) / (y0 - 2*y1 + y2)
    return (peak_bin + delta) * freq_resolution
```

Alternatively, send all 16 low-frequency bins from the Arduino to Python for full-precision interpolation there.

---

## Hann Window

Without windowing, the FFT assumes the signal repeats periodically. A 2.56-second window captures a non-integer number of breath cycles, causing **spectral leakage** — energy from the breathing peak smears into adjacent bins, making regular breathing look irregular.

The Hann window multiplies the time-domain data by a cosine-shaped taper, reducing leakage dramatically. Tradeoff: slight widening of the main lobe (~1.5× wider), which is acceptable for breathing analysis. Applied automatically by the `arduinoFFT` library.

---

## Feature Extraction

### 1. Breathing Rate

Dominant frequency in the band 0.15–1.5 Hz (bins 1–4 at 0.39 Hz resolution). Take the bin with maximum magnitude and apply quadratic interpolation.

| Rate | Class |
|---|---|
| < 0.20 Hz (< 12 br/min) | Bradypnea |
| 0.20–0.33 Hz (12–20 br/min) | Normal eupnea |
| 0.33–0.50 Hz (20–30 br/min) | Mild tachypnea |
| > 0.50 Hz (> 30 br/min) | Tachypnea |

### 2. Spectral Power

Sum of squared FFT magnitudes in the breathing band. Proportional to the square of tidal volume (breath depth). Increases with exercise intensity as depth increases alongside rate.

```
P = Σ |X[k]|² for k in bins 1..8
```

### 3. Regularity Score

Ratio of peak-bin power to total band power:

```
R = |X[peak_bin]|² / P
```

Range 0–1: 0 = completely irregular, 1 = perfectly sinusoidal.

| Value | Interpretation |
|---|---|
| 0.7–0.9 | Regular breathing (rest or steady exercise) |
| 0.5–0.7 | Mildly irregular (variable depth or rate) |
| 0.3–0.5 | Irregular (Cheyne-Stokes, hyperventilation) |
| < 0.3 | Very irregular or apnea |

### 4. Inspiratory-to-Expiratory (I:E) Ratio

In the time domain, inhale causes positive piezo deflection (chest expansion above the DC bias) and exhale causes negative deflection. The fraction of samples above zero reflects the relative duration of each phase.

```python
def ie_ratio(waveform):
    above = np.sum(np.array(waveform) > 0)
    below = np.sum(np.array(waveform) < 0)
    return above / max(below, 1)
```

| I:E | Typical context |
|---|---|
| ~0.5 | Normal rest (inhale ~33% of cycle) |
| ~0.7 | Light exercise |
| ~1.0 | Heavy exercise (equal inhale/exhale) |
| > 1.5 | Obstructive pattern (prolonged inhale) |

---

## Two-Channel Complementarity

The piezo and thermistor provide independent but complementary information:

| Feature | Piezo | Thermistor |
|---|---|---|
| Breathing rate | ✓ Good | ✓ Good |
| Tidal volume | ✓ (amplitude proxy) | ✗ |
| I:E ratio | ✓ (waveform shape) | ✗ |
| Regularity | ✓ (FFT shape) | Partial |
| Apnea detection | Partial (chest may still move in obstructive apnea) | ✓ Best (airflow stops) |
| Motion artefact resistance | Lower | Higher |

Using both channels together is more reliable than either alone. In particular, obstructive sleep apnea can show continued chest movement (the person tries to breathe) while airflow stops entirely — this is only detectable by comparing the two channels, which is exactly what the firmware's apnea detector does.

---

## Exercise-Specific Signal Characteristics

| Parameter | Rest | Light | Moderate | High |
|---|---|---|---|---|
| Breathing rate | 12–16 br/min | 16–24 br/min | 24–36 br/min | 36–50 br/min |
| Spectral power | Low | Medium | High | Very high |
| Tidal volume | Low | Increasing | High | High |
| I:E ratio | ~0.5 | ~0.6 | ~0.7 | ~0.9 |
| Regularity | High | High | Moderate | Variable |

**Recovery rate** is one of the most clinically meaningful metrics. After stopping exercise, breathing rate should return within 2 br/min of baseline within:

- < 2 minutes: excellent cardiovascular fitness
- 2–5 minutes: average fitness
- > 5 minutes: below average

Python tracks this by monitoring the breathing rate time series after the ML classifier transitions from an exercise class back toward normal.