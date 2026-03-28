/*
 * ============================================================
 *  Contactless Breathalyser — Structured 3-Phase Test
 *  Rewritten: 2026-03-08
 * ============================================================
 *
 *  PHASE FLOW (runs once per power-on, then DONE):
 *
 *    [WARMUP - 20s]
 *      Sensor heats up. Readings printed but ignored.
 *
 *    [BASELINE - 20s]
 *      Collects clean-air samples.
 *      Computes mean and std from your prior calibration
 *      as a sanity anchor, then blends with live readings.
 *      At the end, prints computed mean/std/threshold.
 *
 *    [DETECTION - 20s]
 *      Prompts user to blow near sensor.
 *      Compares every reading against threshold.
 *      Uses debounce: N consecutive hits = ALCOHOL DETECTED.
 *      Uses dV/dt: must be rising fast (not just ambient drift).
 *      Locks result (DETECTED / NOT DETECTED) at end of window.
 *
 *    [COOLDOWN - 30s]
 *      Sensor clears. Readings printed. No detection.
 *      Shows live decay back to baseline.
 *
 *    [DONE]
 *      Prints final summary. Halts.
 *
 *  OUTPUT FORMAT (CSV, every sample):
 *    timestamp_ms, raw_adc, voltage_v, phase, note
 *
 *  WIRING:
 *    MQ3 AOUT → A0
 * ============================================================
 */

// ── Pin ──────────────────────────────────────────────────────
const int MQ3_PIN = A0;

// ── Phase Durations ──────────────────────────────────────────
const unsigned long WARMUP_MS    = 20000UL;
const unsigned long BASELINE_MS  = 20000UL;
const unsigned long DETECTION_MS = 20000UL;
const unsigned long COOLDOWN_MS  = 30000UL;

// ── Sampling ─────────────────────────────────────────────────
const unsigned long SAMPLE_MS = 100;   // 100ms between samples = 10 Hz

// ── Prior calibration anchor (from your mq3_samples.csv) ─────
// Used to sanity-check the live baseline. If live baseline
// is wildly different from this, something is wrong.
const float PRIOR_MEAN_ADC = 48.3;    // from CSV (raw ADC units)
const float PRIOR_STD_ADC  = 0.47;    // from CSV

// ── Detection Tuning ─────────────────────────────────────────
const float K_SIGMA        = 4.0;     // threshold = mean + K * std
                                       // 4σ = very conservative, avoids false positives
const int   DEBOUNCE_COUNT = 5;       // consecutive hits needed to confirm
const float DVDT_MIN_ADC   = 2.0;    // min ADC units/sec rise rate
                                       // your calibration showed ~400 ADC/s on alcohol spike
                                       // 2.0 easily filters slow drift, catches real breath

// ── Moving Average ────────────────────────────────────────────
const int AVG_SIZE = 10;
int   avgBuf[AVG_SIZE];
int   avgIdx  = 0;
bool  avgFull = false;

// ── States ────────────────────────────────────────────────────
enum Phase { WARMUP, BASELINE, DETECTION, COOLDOWN, DONE };
Phase phase = WARMUP;

// ── Runtime variables ─────────────────────────────────────────
unsigned long phaseStart     = 0;
unsigned long lastSample     = 0;

// Baseline accumulation
float  baselineSum   = 0;
float  baselineSumSq = 0;
int    baselineCount = 0;
float  liveMean      = 0;
float  liveStd       = 0;
float  threshold     = 0;

// Detection
int   hitCount       = 0;
bool  alcoholResult  = false;   // final result for this test session

// dV/dt
float prevADC        = 0;
unsigned long prevT  = 0;

// ── Helpers ──────────────────────────────────────────────────

float smoothADC(int newVal) {
  avgBuf[avgIdx] = newVal;
  avgIdx = (avgIdx + 1) % AVG_SIZE;
  if (avgIdx == 0) avgFull = true;
  int count = avgFull ? AVG_SIZE : avgIdx;
  float sum = 0;
  for (int i = 0; i < count; i++) sum += avgBuf[i];
  return sum / count;
}

// Print a labelled CSV data row
void printRow(unsigned long ts, int raw, float smoothed, const char* phaseName, const char* note) {
  float voltage = raw * (5.0 / 1023.0);
  Serial.print(ts);         Serial.print(",");
  Serial.print(raw);        Serial.print(",");
  Serial.print(voltage, 4); Serial.print(",");
  Serial.print(smoothed, 2);Serial.print(",");
  Serial.print(phaseName);  Serial.print(",");
  Serial.println(note);
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  pinMode(MQ3_PIN, INPUT);

  Serial.println("# ============================================");
  Serial.println("# Contactless Breathalyser — Structured Test");
  Serial.println("# ============================================");
  Serial.println("#");
  Serial.print("# Prior calibration anchor: mean=");
  Serial.print(PRIOR_MEAN_ADC);
  Serial.print(" ADC, std=");
  Serial.print(PRIOR_STD_ADC);
  Serial.println(" ADC");
  Serial.print("# K_SIGMA="); Serial.print(K_SIGMA);
  Serial.print("  DEBOUNCE="); Serial.print(DEBOUNCE_COUNT);
  Serial.print("  DVDT_MIN="); Serial.print(DVDT_MIN_ADC);
  Serial.println(" ADC/s");
  Serial.println("#");
  Serial.println("# PHASE 1: WARMUP (20s) — sensor heating up...");
  Serial.println("#");
  Serial.println("timestamp_ms,raw_adc,voltage_v,smoothed_adc,phase,note");

  phaseStart = millis();
  lastSample = millis();
  prevT      = millis();
  prevADC    = analogRead(MQ3_PIN);
}

// ── Main Loop ─────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── Sample gate ──────────────────────────────────────────
  if (now - lastSample < SAMPLE_MS) return;
  lastSample = now;

  // ── Read sensor ──────────────────────────────────────────
  int   rawADC    = analogRead(MQ3_PIN);
  float smoothed  = smoothADC(rawADC);

  // ── dV/dt (in ADC units per second) ──────────────────────
  float dt   = (now - prevT) / 1000.0;
  float dvdt = (dt > 0) ? (smoothed - prevADC) / dt : 0;
  prevADC = smoothed;
  prevT   = now;

  unsigned long elapsed = now - phaseStart;

  // ══════════════════════════════════════════════════════════
  //  PHASE: WARMUP
  // ══════════════════════════════════════════════════════════
  if (phase == WARMUP) {
    printRow(now, rawADC, smoothed, "WARMUP", "heating");

    if (elapsed >= WARMUP_MS) {
      phase      = BASELINE;
      phaseStart = now;

      Serial.println("#");
      Serial.println("# ============================================");
      Serial.println("# PHASE 2: BASELINE (20s)");
      Serial.println("# Keep sensor in clean air. Do NOT bring");
      Serial.println("# alcohol near the sensor during this phase.");
      Serial.println("# ============================================");
      Serial.println("#");
    }
  }

  // ══════════════════════════════════════════════════════════
  //  PHASE: BASELINE — collect clean-air stats
  // ══════════════════════════════════════════════════════════
  else if (phase == BASELINE) {

    // Accumulate for mean and std calculation
    baselineSum   += smoothed;
    baselineSumSq += smoothed * smoothed;
    baselineCount++;

    // Print countdown every second
    char note[24];
    int secsLeft = (BASELINE_MS - elapsed) / 1000;
    sprintf(note, "baseline %ds left", secsLeft);
    printRow(now, rawADC, smoothed, "BASELINE", note);

    if (elapsed >= BASELINE_MS) {
      // ── Compute live mean and std ─────────────────────────
      liveMean = baselineSum / baselineCount;
      float variance = (baselineSumSq / baselineCount) - (liveMean * liveMean);
      liveStd  = sqrt(variance);

      // ── Sanity check against prior calibration ────────────
      // If live mean is >5 ADC away from prior, warn user.
      // Still proceed with live values (trust the sensor).
      bool driftWarning = abs(liveMean - PRIOR_MEAN_ADC) > 5.0;

      // ── Compute threshold ─────────────────────────────────
      threshold = liveMean + K_SIGMA * liveStd;

      Serial.println("#");
      Serial.println("# ── BASELINE RESULT ──────────────────────────");
      Serial.print("# Live   mean  : "); Serial.print(liveMean, 4); Serial.println(" ADC");
      Serial.print("# Live   std   : "); Serial.print(liveStd, 4);  Serial.println(" ADC");
      Serial.print("# Prior  mean  : "); Serial.print(PRIOR_MEAN_ADC); Serial.println(" ADC");
      Serial.print("# Prior  std   : "); Serial.print(PRIOR_STD_ADC);  Serial.println(" ADC");
      Serial.print("# Threshold    : mean + "); Serial.print(K_SIGMA);
      Serial.print("s = "); Serial.print(threshold, 4); Serial.println(" ADC");
      if (driftWarning) {
        Serial.println("# WARNING: Live baseline differs >5 ADC from");
        Serial.println("# prior calibration. Check ambient conditions.");
      } else {
        Serial.println("# Baseline matches prior calibration. Good.");
      }
      Serial.println("# ─────────────────────────────────────────────");

      // ── Transition to DETECTION ───────────────────────────
      phase      = DETECTION;
      phaseStart = now;
      hitCount   = 0;
      alcoholResult = false;

      Serial.println("#");
      Serial.println("# ============================================");
      Serial.println("# PHASE 3: DETECTION (20s)");
      Serial.println("# >>> BLOW near the sensor now! <<<");
      Serial.println("# You have 20 seconds. Blow steadily at ~5cm.");
      Serial.println("# ============================================");
      Serial.println("#");
    }
  }

  // ══════════════════════════════════════════════════════════
  //  PHASE: DETECTION
  // ══════════════════════════════════════════════════════════
  else if (phase == DETECTION) {

    bool aboveThreshold = (smoothed > threshold);
    bool risingFast     = (dvdt > DVDT_MIN_ADC);
    const char* note;

    if (!alcoholResult) {
      // Not yet confirmed — keep evaluating
      if (aboveThreshold && risingFast) {
        hitCount++;
        note = "RISING";
      } else if (aboveThreshold) {
        // Above threshold but not rising fast — likely residual, don't count up
        hitCount = max(0, hitCount - 1);
        note = "above-slow";
      } else {
        hitCount = 0;
        note = "clear";
      }

      if (hitCount >= DEBOUNCE_COUNT) {
        alcoholResult = true;
        Serial.println("#");
        Serial.println("# !!! ALCOHOL DETECTED !!!");
        Serial.print("# Confirmed at t="); Serial.print(now); Serial.println("ms");
        Serial.println("#");
      }
    } else {
      note = "ALCOHOL-CONFIRMED";
    }

    // Print countdown every second in the note when no event
    char noteBuf[32];
    int secsLeft = (DETECTION_MS - elapsed) / 1000;
    if (!alcoholResult && hitCount == 0) {
      sprintf(noteBuf, "clear %ds left", secsLeft);
      note = noteBuf;
    }

    printRow(now, rawADC, smoothed, "DETECTION", note);

    // Print reminder halfway through if nothing detected yet
    if (elapsed >= DETECTION_MS / 2 && elapsed < DETECTION_MS / 2 + SAMPLE_MS && !alcoholResult) {
      Serial.println("# Reminder: 10s left — blow near the sensor!");
    }

    if (elapsed >= DETECTION_MS) {
      if (!alcoholResult) {
        Serial.println("#");
        Serial.println("# RESULT: NO alcohol detected in test window.");
        Serial.println("#");
      }

      phase      = COOLDOWN;
      phaseStart = now;

      Serial.println("# ============================================");
      Serial.println("# PHASE 4: COOLDOWN (30s)");
      Serial.println("# Remove alcohol source. Sensor clearing...");
      Serial.println("# ============================================");
      Serial.println("#");
    }
  }

  // ══════════════════════════════════════════════════════════
  //  PHASE: COOLDOWN
  // ══════════════════════════════════════════════════════════
  else if (phase == COOLDOWN) {

    // Show how far the reading has decayed toward baseline
    float pctRecovered = 0;
    if (smoothed > liveMean) {
      float peakEstimate = threshold * 2;   // rough estimate of peak
      pctRecovered = 100.0 * (1.0 - (smoothed - liveMean) / (peakEstimate - liveMean));
      pctRecovered = constrain(pctRecovered, 0, 100);
    } else {
      pctRecovered = 100.0;
    }

    char note[32];
    int secsLeft = (COOLDOWN_MS - elapsed) / 1000;
    sprintf(note, "cooling %ds left", secsLeft);
    printRow(now, rawADC, smoothed, "COOLDOWN", note);

    if (elapsed >= COOLDOWN_MS) {
      phase = DONE;

      Serial.println("#");
      Serial.println("# ============================================");
      Serial.println("# TEST COMPLETE — SUMMARY");
      Serial.println("# ============================================");
      Serial.print("# Baseline mean      : "); Serial.print(liveMean, 2); Serial.println(" ADC");
      Serial.print("# Baseline std       : "); Serial.print(liveStd, 4);  Serial.println(" ADC");
      Serial.print("# Detection threshold: "); Serial.print(threshold, 2); Serial.println(" ADC");
      Serial.print("# FINAL RESULT       : ");
      Serial.println(alcoholResult ? "ALCOHOL DETECTED" : "NO ALCOHOL DETECTED");
      Serial.println("# ============================================");
      Serial.println("# Reset Arduino to run another test.");
      Serial.println("#");
    }
  }

  // ══════════════════════════════════════════════════════════
  //  PHASE: DONE — halt gracefully
  // ══════════════════════════════════════════════════════════
  else if (phase == DONE) {
    // Just keep printing live readings so you can watch recovery
    printRow(now, rawADC, smoothed, "DONE", "monitoring-only");
  }
}
