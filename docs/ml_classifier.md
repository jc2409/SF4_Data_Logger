# Machine Learning Classifier

## Overview

A scikit-learn **Random Forest** classifies the current breathing pattern from the feature vector streamed by the Arduino. The model is trained offline during week 1–2 using labelled data you collect yourself, then saved and loaded at runtime for real-time inference.

Random Forest is chosen because it handles small datasets well, produces directly interpretable feature importances, and requires no GPU. Inference takes ~1 ms on any laptop.

---

## Breathing Pattern Classes

| Class | Clinical name | Description | Rate |
|---|---|---|---|
| `normal` | Eupnea | Quiet rest breathing | 12–20 br/min |
| `light_exercise` | — | Elevated rate, regular | 20–28 br/min |
| `heavy_exercise` | — | High rate, high depth | 30–50 br/min |
| `recovery` | — | Decreasing rate post-exercise | Falling |
| `tachypnea` | Tachypnea | Fast, shallow — anxiety/fever | > 20 br/min |
| `irregular` | Dysrhythmia | Variable rate and depth | Any |

Six classes is achievable with good training data. If time is short, reduce to four: `normal`, `exercise`, `recovery`, `irregular`.

---

## Core Features (Two-Sensor System)

Features extracted from the Arduino DATA packet:

| Feature | Source | Meaning |
|---|---|---|
| `breathing_rate` Hz | Piezo FFT | Dominant breathing frequency |
| `spectral_power` | Piezo FFT | Tidal volume proxy |
| `regularity` | Piezo FFT | Waveform periodicity |
| `ie_ratio` | Piezo waveform | Inhale vs exhale duration |
| `apnea_flag` | Thermistor | Airflow present/absent |

**Derived features added in Python** (from the last N readings):

| Derived feature | Formula | Meaning |
|---|---|---|
| `breathing_rate_bpm` | `hz × 60` | Human-readable rate |
| `rate_power` | `rate × power` | Exercise intensity proxy |
| `br_rolling_mean` | Mean of last 3 | Smoothed rate |
| `br_delta` | Difference from last reading | Rate of change |
| `regularity_class` | `int(regularity × 3)` | Discretised regularity |

If the SpO2 extension is fitted, add `spo2`, `heart_rate`, `spo2_deviation` (98 − spo2), and `hr_br_ratio` (HR / br_bpm) to the feature vector.

---

## Training Data Collection

### Session Protocol

Aim for at least 8 minutes of labelled data per class:

```
Class: normal
  Sit quietly, breathe naturally for 10 minutes
  Label: "normal"

Class: light_exercise
  Walk on the spot continuously for 10 minutes
  Label: "light_exercise"

Class: heavy_exercise
  Jumping jacks or jogging for 5–8 minutes
  Label: "heavy_exercise"

Class: recovery
  Immediately after heavy_exercise, sit down
  Record for 10 minutes without prompting
  Label: "recovery"

Class: tachypnea (simulated)
  Breathe deliberately fast and shallow (~25 br/min)
  for 5 minutes
  Label: "tachypnea"

Class: irregular
  Vary rate and depth randomly, pause briefly,
  sigh, resume — for 5 minutes
  Label: "irregular"
```

### Data Logging Script

```python
# python/collect_training_data.py

import csv, time, sys
from serial_handler import SerialHandler, find_arduino_port
from feature_extractor import parse_data_packet

def collect(label: str, duration_s: int = 480):
    port = find_arduino_port()
    if not port:
        print("Arduino not found."); sys.exit(1)

    handler = SerialHandler(port)
    handler.connect()

    fname = f"training_{label}_{int(time.time())}.csv"
    with open(fname, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow([
            'breathing_rate', 'spectral_power', 'regularity',
            'ie_ratio', 'apnea_flag', 'label', 'timestamp'
        ])
        start = time.time()
        print(f"Recording '{label}' for {duration_s}s — Ctrl+C to stop early.")
        while time.time() - start < duration_s:
            msg = handler.get_message(timeout=3.0)
            if msg:
                feat = parse_data_packet(msg)
                if feat:
                    writer.writerow([
                        feat.breathing_rate, feat.spectral_power,
                        feat.regularity, feat.ie_ratio,
                        feat.apnea_flag, label, feat.timestamp
                    ])
                    bpm = feat.breathing_rate * 60
                    print(f"  {bpm:.1f} br/min  reg={feat.regularity:.2f}  ie={feat.ie_ratio:.2f}")

    handler.disconnect()
    print(f"Saved → {fname}")

if __name__ == "__main__":
    label = sys.argv[1] if len(sys.argv) > 1 else "normal"
    collect(label)
```

Usage:
```bash
python collect_training_data.py normal
python collect_training_data.py heavy_exercise
python collect_training_data.py recovery
# ... etc
```

---

## Feature Engineering

```python
# python/feature_extractor.py  (add this function)

import pandas as pd
import numpy as np

def engineer_features(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()
    df['breathing_rate_bpm'] = df['breathing_rate'] * 60.0
    df['rate_power']         = df['breathing_rate'] * df['spectral_power']
    df['br_rolling_mean']    = df['breathing_rate'].rolling(3, min_periods=1).mean()
    df['br_delta']           = df['breathing_rate'].diff().fillna(0)
    df['regularity_class']   = (df['regularity'] * 3).astype(int).clip(0, 3)

    # Add SpO2 features only if column exists (optional extension)
    if 'spo2' in df.columns:
        df['spo2_deviation'] = 98.0 - df['spo2']
        df['hr_br_ratio']    = df['heart_rate'] / (df['breathing_rate_bpm'] + 1e-6)

    return df
```

---

## Model Training

```python
# python/train_classifier.py

import glob, joblib
import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import StratifiedKFold, cross_val_score
from sklearn.preprocessing import StandardScaler
from sklearn.pipeline import Pipeline
from sklearn.metrics import classification_report

CORE_FEATURES = [
    'breathing_rate', 'spectral_power', 'regularity', 'ie_ratio',
    'breathing_rate_bpm', 'rate_power', 'br_rolling_mean', 'br_delta',
    'regularity_class'
]

def load_data(data_dir='training_data/') -> pd.DataFrame:
    frames = [pd.read_csv(f) for f in glob.glob(f"{data_dir}*.csv")]
    if not frames:
        raise FileNotFoundError(f"No CSV files found in {data_dir}")
    return pd.concat(frames, ignore_index=True)

def train_and_save(data_dir='training_data/', output='models/respiratory_rf.pkl'):
    from feature_extractor import engineer_features

    df = engineer_features(load_data(data_dir)).dropna()

    # Determine feature columns — include SpO2 features if present
    feature_cols = CORE_FEATURES.copy()
    if 'spo2' in df.columns:
        feature_cols += ['spo2_deviation', 'hr_br_ratio', 'spo2', 'heart_rate']

    X = df[feature_cols].values
    y = df['label'].values

    print(f"Dataset: {len(X)} samples, {len(np.unique(y))} classes")
    for cls in np.unique(y):
        print(f"  {cls}: {(y == cls).sum()} samples")

    model = Pipeline([
        ('scaler', StandardScaler()),
        ('rf', RandomForestClassifier(
            n_estimators    = 200,
            max_depth       = 12,
            min_samples_leaf = 3,
            class_weight    = 'balanced',
            random_state    = 42,
            n_jobs          = -1
        ))
    ])

    cv_scores = cross_val_score(model, X, y, cv=StratifiedKFold(5), scoring='f1_weighted')
    print(f"\nCross-validation F1: {cv_scores.mean():.3f} ± {cv_scores.std():.3f}")

    model.fit(X, y)

    # Feature importances
    rf = model.named_steps['rf']
    print("\nFeature importances:")
    for name, imp in sorted(zip(feature_cols, rf.feature_importances_), key=lambda x: -x[1]):
        print(f"  {name:<28} {imp:.3f}")

    import os; os.makedirs(os.path.dirname(output), exist_ok=True)
    joblib.dump({'model': model, 'features': feature_cols}, output)
    print(f"\nSaved to {output}")

if __name__ == "__main__":
    train_and_save()
```

---

## Real-Time Inference

```python
# python/classifier.py

import joblib
import numpy as np
from collections import deque
from feature_extractor import RespiratoryFeatures

class BreathingClassifier:
    def __init__(self, model_path: str):
        saved = joblib.load(model_path)
        self.model        = saved['model']
        self.feature_cols = saved['features']
        self._history     = deque(maxlen=3)

    @classmethod
    def load(cls, path: str) -> 'BreathingClassifier':
        return cls(path)

    def predict(self, feat: RespiratoryFeatures) -> dict:
        self._history.append(feat)
        x = self._build_vector(feat).reshape(1, -1)

        label       = self.model.predict(x)[0]
        probs       = self.model.predict_proba(x)[0]
        classes     = self.model.classes_
        confidence  = probs.max()

        return {
            'class':         label,
            'confidence':    float(confidence),
            'probabilities': dict(zip(classes, probs.tolist()))
        }

    def _build_vector(self, feat: RespiratoryFeatures) -> np.ndarray:
        bpm        = feat.breathing_rate * 60.0
        hist_bpm   = np.mean([f.breathing_rate * 60 for f in self._history])
        br_delta   = (feat.breathing_rate - self._history[-2].breathing_rate
                      if len(self._history) >= 2 else 0.0)
        reg_class  = int(min(feat.regularity * 3, 3))

        core = [
            feat.breathing_rate,
            feat.spectral_power,
            feat.regularity,
            feat.ie_ratio,
            bpm,
            feat.breathing_rate * feat.spectral_power,
            hist_bpm,
            br_delta,
            reg_class
        ]

        # Append SpO2 features if extension present and model expects them
        if len(self.feature_cols) > 9 and feat.spo2 is not None:
            core += [
                98.0 - feat.spo2,
                (feat.heart_rate or 0) / (bpm + 1e-6),
                feat.spo2,
                feat.heart_rate or 0
            ]
        elif len(self.feature_cols) > 9:
            core += [0, 0, 98, 72]  # neutral placeholder if sensor not fitted

        return np.array(core[:len(self.feature_cols)])
```

---

## What to Report on the Model

In your report, include:

1. **Confusion matrix** — which classes are confused and why (e.g. normal vs bradypnea at very slow rates share similar spectral profiles)
2. **Per-class F1 scores** — identify weak classes and justify the confusion physiologically
3. **Feature importances** — relate the top features back to the underlying physics (breathing rate dominates because it directly encodes intensity; regularity distinguishes resting from disturbed patterns)
4. **Training data summary** — samples per class, session duration, how data was labelled
5. **Cross-validation methodology** — why stratified k-fold is used rather than a simple hold-out split for small datasets