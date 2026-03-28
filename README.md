# 🫁 Non-Contact Breathalyzer

An Arduino-based contactless breathalyzer that uses an **MQ3** (alcohol vapour) and **MQ135** (air quality / breath confirmation) sensor together to detect alcohol in breath — without requiring the subject to blow into a tube.

---

## 📁 Repository Structure

```
├── Breathalyzer_Final/            # Main breathalyzer firmware (MQ3 + MQ135 + fan control)
├── MQ3_Testing_CSV/               # MQ3 sensor data collection + Python CSV logger
├── MQ135_Data_Collection_CSV/     # MQ135 sensor data collection + Python CSV logger
└── README.md
```

---

## How It Works

The system uses two sensors in combination to eliminate false positives from ambient alcohol sources (e.g. perfume, hand sanitiser):

- **MQ3** detects alcohol vapour
- **MQ135** confirms that an actual breath was present

A verdict is reached by comparing both sensor readings against a clean-air baseline:

| MQ3 | MQ135 | Result |
|-----|-------|--------|
| Rose | Rose | ✅ Alcohol detected in breath |
| Rose | Flat | ⚠️ Ambient vapour — not a breath |
| Flat | Rose | ✅ Normal breath, no alcohol |
| Flat | Flat | ❌ Nothing detected — breathe closer |

A **5V fan** (driven via BJT on pin D6) automatically turns on during the breath test and purge phases to draw breath over the sensors, then shuts off.

---

## ⚙️ Hardware

| Component | Pin |
|-----------|-----|
| MQ3 sensor | A0 |
| MQ135 sensor | A1 |
| 5V fan (via NPN BJT) | D6 (PWM) |
| Arduino Uno R3 | — |

### Fan Wiring (BJT + Flyback Diode)

```
+5V ──────────────┬──── Fan+ (M)
                  │     Fan− (M)
                 D1          │
              (1N4007)   Collector
                  │     Q1 NPN (2N2222)
                  └──── Collector
                         Base ◄──[1kΩ RB]──── Arduino D6
                         Emitter
                              │
                             GND
```

| Connection | Detail |
|---|---|
| Fan (+) | +5V |
| Fan (−) | BJT Collector (Q1) |
| BJT Base | Arduino D6 via 1kΩ resistor (RB) |
| BJT Emitter | GND |
| D1 1N4007 Anode | Fan (−) / Collector side |
| D1 1N4007 Cathode | Fan (+) / +5V side |

> **Why the 1N4007?** The fan motor is an inductive load. When the BJT switches off, the collapsing magnetic field generates a reverse voltage spike that can destroy the transistor. The flyback diode clamps that spike and safely recirculates the current. Without it, repeated switching will eventually kill the 2N2222.

---

## 📄 `Breathalyzer_Final` — Main Breathalyzer Firmware

The production firmware. Runs a repeating 4-phase cycle:

```
Warm-up (30s) → Baseline (30s) → Breath Test (20s) → Purge (30s) → repeat
```

### Phases

**Warm-up** — Sensors stabilise in clean air. Fan is off.

**Baseline** — Samples both sensors at 10 Hz for 30 seconds to establish a clean-air average. Thresholds are computed automatically:
```
threshold = baseline_average × (1 + RISE_PCT / 100)
```

**Breath Test** — Fan turns on. Subject breathes toward the sensors. A rolling average (window = 5 samples) smooths out noise. The peak of the rolling average is compared against both thresholds. Verdict is printed at the end.

**Purge** — Fan stays on to clear residual vapour from the sensor chamber before the next cycle begins.

### Tunable Parameters

```cpp
const float MQ3_RISE_PCT   = 10.0;  // % above baseline to count as alcohol
const float MQ135_RISE_PCT = 5.0;   // % above baseline to confirm breath
const int   FAN_SPEED      = 200;   // Fan PWM duty cycle (0–255)
const int   ROLLING_WINDOW = 5;     // Smoothing window (number of samples)
```

Raise `MQ3_RISE_PCT` if you get false positives from ambient air. Lower it if real breath events are being missed.

### Serial Output (9600 baud)

```
================================================
PHASE: BREATH TEST
================================================
Please breathe toward the sensors now.
Fan ON.

[BREATH] T-20s  |  MQ3:  ADC=312  V=1.524  dADC=45.2  |  MQ135: ADC=198  V=0.967  dADC=12.1
[BREATH] T-19s  |  MQ3:  ADC=341  V=1.665  dADC=74.1  |  MQ135: ADC=221  V=1.079  dADC=35.4
...

Fan OFF.

  ---- RESULT ----
  Peak rolling-avg MQ3  : ADC=387.4  threshold=352.0  [ROSE]
  Peak rolling-avg MQ135: ADC=210.3  threshold=194.1  [ROSE]

  ================================
  >> *** ALCOHOL DETECTED IN BREATH ***
     MQ3 rose (alcohol vapour) AND MQ135 rose (breath confirmed).
     Alcohol is present in the breath.
  ================================
```

---

## 📄 `MQ3_Testing_CSV` — MQ3 Sensor Data Collection

Used during development to characterise the MQ3 sensor's response to alcohol. Runs a one-time sequence:

```
Warm-up (20s) → Baseline (30s) → Alcohol exposure (30s) → Cooldown (30s) → DONE
```

Outputs CSV rows over Serial at **115200 baud**:

```
timestamp_ms,raw_adc,voltage,phase
20045,112,0.5474,baseline
21045,114,0.5571,baseline
...
51200,743,3.6266,alcohol
...
81400,210,1.0254,cooldown
```

The built-in LED (pin 13) blinks with each sample and stays solid when the run is complete.

### Python Logger — `calibrate.py`

Reads the Serial stream, saves a raw CSV, and prints per-phase statistics.

**Install dependencies:**
```bash
pip install pyserial pandas
```

**Run with hardware connected:**
```bash
python calibrate.py --port COM3          # Windows
python calibrate.py --port /dev/ttyUSB0  # Linux / Mac
```

**Analyse an existing CSV without hardware:**
```bash
python calibrate.py --analyse-only mq3_samples.csv
python calibrate.py --analyse-only mq3_samples.csv --k 2.5   # custom sigma multiplier
```

**Example statistics output:**
```
=======================================================
PHASE        COUNT   MEAN_V    STD_V    MIN_V    MAX_V
=======================================================
alcohol        300   2.8341   0.6123   0.9824   3.6266
baseline       300   0.5512   0.0184   0.5180   0.5913
cooldown       300   1.0471   0.2340   0.5620   1.8832
=======================================================

[THRESHOLD]  mean + 2.0σ  =  0.5512 + 2×0.0184  =  0.5880 V
[SEPARATION] alcohol_mean − threshold  =  +2.2461 V  ✓ detectable
```

A `calibration_config.txt` is saved alongside the CSV with the computed threshold values.

---

## 📄 `MQ135_Data_Collection_CSV` — MQ135 Sensor Data Collection

Used to characterise the MQ135 sensor's response to breath versus perfume (alcohol-containing ambient vapour). Runs a one-time sequence at **2 Hz** (60 samples per window):

```
Warm-up (30s) → Perfume window (30s) → Cooldown (2 min) → Breath window (30s) → DONE
```

Outputs labelled rows over Serial at **9600 baud**:

```
STATUS,Warming up...
STATUS,Starting perfume measurement...
PERFUME,1,95.00
PERFUME,2,94.00
...
STATUS,Perfume done. Cooling down...
STATUS,Cooldown done. Starting breath measurement...
BREATH,1,122.00
BREATH,2,124.00
...
DONE
```

### Python Logger — `mq135_logger.py`

Captures the Serial stream and saves a CSV. Stops automatically when the Arduino sends `DONE`.

**Install dependencies:**
```bash
pip install pyserial
```

**Edit the port at the top of the script, then run:**
```bash
python mq135_logger.py
```

**Output CSV format:**
```
Timestamp,Type,Reading #,Sensor Value
23:49:18,PERFUME,1,95.00
23:49:23,PERFUME,12,127.00
...
23:51:51,BREATH,8,179.00
23:51:52,BREATH,9,193.00
```

### Key Findings from Collected Data

Analysis across 5 trials identified three features that cleanly separate perfume from breath:

| Feature | Perfume | Breath |
|---------|---------|--------|
| Peak value (ADC) | 600 – 868 | 180 – 320 |
| Standard deviation | 77 – 295 | 24 – 30 |
| Signal shape | Sharp spike + slow decay | Rapid rhythmic oscillation |

The classifier rule used in the main firmware:

```
if peak > 400  AND  std_dev > 80  →  PERFUME / ambient vapour
if peak < 350  AND  std_dev < 40  →  Breath confirmed
otherwise                         →  UNCERTAIN
```

> **Note:** The first 10 samples of each perfume window are excluded from analysis because perfume was introduced approximately 10 samples in across all trials.

---

## 🚀 Getting Started

### Requirements

- Arduino IDE 1.8+ or Arduino IDE 2.x
- Python 3.8+
- `pyserial` — `pip install pyserial`
- `pandas` — `pip install pandas` (for `calibrate.py` only)

### Running the Breathalyzer

1. Wire up MQ3 (A0), MQ135 (A1), and the fan circuit (D6) as shown above
2. Flash `Breathalyzer_Final` to the Arduino Uno
3. Open Serial Monitor at **9600 baud**
4. Keep sensors in clean air during warm-up and baseline phases
5. Breathe toward the sensors when prompted during the breath test

### Running the Data Collection Scripts

1. Flash the relevant Arduino sketch (`MQ3_Testing_CSV` or `MQ135_Data_Collection_CSV`)
2. Note the correct COM port from Arduino IDE → Tools → Port
3. Edit the `PORT` variable at the top of the corresponding Python script
4. Run the Python logger — it captures the full session and saves the CSV automatically

---

## 📝 Notes

- Allow **at least 5 minutes** of warm-up in real conditions for the MQ3 to fully stabilise — the 30s warm-up in firmware is a hardware minimum
- Sensor readings vary with ambient temperature and humidity — recalibrate if the environment changes significantly
- `MQ3_RISE_PCT` and `MQ135_RISE_PCT` may need adjustment depending on your specific sensor batch and enclosure design
- Ensure airflow from the fan is directed across both sensor faces for consistent breath capture
- The 1N4007 flyback diode across the fan is not optional — omitting it risks damaging the 2N2222 BJT over time