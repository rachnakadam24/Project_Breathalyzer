// ============================================================
//  Non-Contact Breathalyzer — Simple Logic
//  Board  : Arduino Uno R3
//  MQ3    : A0  (alcohol vapour)
//  MQ135  : A1  (air quality / breath confirmation)
//
//  CYCLE:
//    1. Warm-up      : 30s  (sensor stabilisation)
//    2. Baseline     : 30s  (clean-air reference, 10 Hz sampling)
//    3. Breath Test  : 20s  (compare to baseline → verdict)
//    4. Purge        : 30s  (sensors clear before next cycle)
//    5. Repeat from step 2
// ============================================================

#define MQ3_PIN   A0
#define MQ135_PIN A1

// ── Sensitivity: % rise above baseline to count as a real change ──
//
//  MQ3_RISE_PCT   — how many % above the MQ3 clean-air average
//                   counts as "alcohol vapour detected"
//                   e.g. 10.0 means the reading must be >10% higher
//                   than the baseline average
//
//  MQ135_RISE_PCT — same idea for MQ135 ("real breath" confirmation)
//
//  These are the ONLY two values you may want to tweak.
//  Raise them if you get too many false positives.
//  Lower them if the sensor misses real events.
//
const float MQ3_RISE_PCT   = 10.0;   // % above MQ3   baseline avg
const float MQ135_RISE_PCT = 5.0;    // % above MQ135 baseline avg

// ── Sampling ────────────────────────────────────────────────
const int   BASELINE_DURATION_S  = 30;
const int   BREATH_DURATION_S    = 20;
const int   WARMUP_DURATION_S    = 30;
const int   PURGE_DURATION_S     = 30;
const int   SAMPLE_HZ            = 10;
const int   SAMPLE_INTERVAL_MS   = 1000 / SAMPLE_HZ;  // 100 ms

// ── Rolling average window ───────────────────────────────────
//  How many samples to average together at each moment.
//  At 10 Hz, window=5 means each rolling average covers 0.5 seconds.
//  Increase to smooth more. Decrease to react faster.
const int ROLLING_WINDOW = 5;

// Circular buffers that hold the last ROLLING_WINDOW readings
float rollingMQ3[ROLLING_WINDOW];
float rollingMQ135[ROLLING_WINDOW];
int   rollingIndex = 0;   // points to the oldest slot (next to overwrite)

// ── Baseline struct returned by doBaseline() ─────────────────
struct Baseline {
  float avgMQ3;    // average ADC over the baseline window
  float avgMQ135;
  float threshMQ3;    // avgMQ3   * (1 + MQ3_RISE_PCT/100)
  float threshMQ135;  // avgMQ135 * (1 + MQ135_RISE_PCT/100)
};

// ============================================================
void setup() {
  Serial.begin(9600);
  initRollingBuffers(0.0);   // zero-fill buffers before first use
  printHeader("NON-CONTACT BREATHALYZER");
  doWarmup();
}

// ============================================================
void loop() {
  Baseline bl = doBaseline();
  doBreathTest(bl);
  doPurge();
}

// ============================================================
//  PHASE 1 — WARM-UP
// ============================================================
void doWarmup() {
  printHeader("PHASE: WARM-UP");
  Serial.println(F("Keep sensors in clean air."));
  Serial.println(F(""));

  for (int t = WARMUP_DURATION_S; t > 0; t--) {
    int   rawMQ3   = analogRead(MQ3_PIN);
    int   rawMQ135 = analogRead(MQ135_PIN);
    float vMQ3     = adcToVoltage(rawMQ3);
    float vMQ135   = adcToVoltage(rawMQ135);

    printSensorLine("WARM-UP", t, rawMQ3, vMQ3, rawMQ135, vMQ135);
    delay(1000);
  }

  Serial.println(F(""));
  Serial.println(F("Warm-up complete!"));
}

// ============================================================
//  PHASE 2 — BASELINE SAMPLING  (10 Hz for 30 s)
//  Returns a Baseline struct containing the averages and the
//  auto-calculated thresholds derived from them.
//  No manual threshold constants needed.
// ============================================================
Baseline doBaseline() {
  printHeader("PHASE: BASELINE");
  Serial.println(F("Measuring clean-air baseline. Do NOT breathe on sensors."));
  Serial.println(F(""));

  long sumMQ3     = 0;
  long sumMQ135   = 0;
  int  totalSamples = BASELINE_DURATION_S * SAMPLE_HZ;
  int  lastSecond   = BASELINE_DURATION_S;

  for (int s = 0; s < totalSamples; s++) {
    int rawMQ3   = analogRead(MQ3_PIN);
    int rawMQ135 = analogRead(MQ135_PIN);

    sumMQ3   += rawMQ3;
    sumMQ135 += rawMQ135;

    // Print once per second (every SAMPLE_HZ samples)
    if (s % SAMPLE_HZ == 0) {
      printSensorLine("BASELINE", lastSecond,
                      rawMQ3,   adcToVoltage(rawMQ3),
                      rawMQ135, adcToVoltage(rawMQ135));
      lastSecond--;
    }

    delay(SAMPLE_INTERVAL_MS);
  }

  // ── Build and return the Baseline struct ──────────────────
  Baseline bl;
  bl.avgMQ3   = (float)sumMQ3   / totalSamples;
  bl.avgMQ135 = (float)sumMQ135 / totalSamples;

  // Threshold = average + X% of average
  //   e.g. avg=400, MQ3_RISE_PCT=10  →  threshold = 400 * 1.10 = 440
  // Any reading above this during the breath phase counts as a "rise"
  bl.threshMQ3   = bl.avgMQ3   * (1.0 + MQ3_RISE_PCT   / 100.0);
  bl.threshMQ135 = bl.avgMQ135 * (1.0 + MQ135_RISE_PCT / 100.0);

  Serial.println(F(""));
  Serial.print(F("  MQ3   avg: ADC=")); Serial.print(bl.avgMQ3,   1);
  Serial.print(F("  V="));             Serial.print(adcToVoltage(bl.avgMQ3),   3);
  Serial.print(F("  →  rise threshold ADC=")); Serial.println(bl.threshMQ3, 1);

  Serial.print(F("  MQ135 avg: ADC=")); Serial.print(bl.avgMQ135, 1);
  Serial.print(F("  V="));             Serial.print(adcToVoltage(bl.avgMQ135), 3);
  Serial.print(F("  →  rise threshold ADC=")); Serial.println(bl.threshMQ135, 1);

  Serial.println(F("Baseline complete!"));
  return bl;
}

// ============================================================
//  PHASE 3 — BREATH TEST  (20 s)
//
//  Rolling average logic:
//    Every new sample, we drop the oldest reading from the
//    window and add the newest (circular buffer).
//    We compute the rolling average at each step, then track
//    the PEAK of that rolling average over the full 20s window.
//
//    This means:
//      • A single noisy ADC spike gets diluted by (ROLLING_WINDOW-1)
//        clean readings → cannot trigger a false positive alone
//      • A real breath sustained for >0.5s pushes the rolling
//        average above threshold reliably → detected correctly
// ============================================================
void doBreathTest(Baseline bl) {
  printHeader("PHASE: BREATH TEST");
  Serial.println(F("Please breathe toward the sensors now."));
  Serial.print(F("  (MQ3 must exceed ADC="));  Serial.print(bl.threshMQ3,   1);
  Serial.print(F(" | MQ135 must exceed ADC=")); Serial.print(bl.threshMQ135, 1);
  Serial.print(F(" | rolling window="));        Serial.print(ROLLING_WINDOW);
  Serial.println(F(" samples)"));
  Serial.println(F(""));

  // Pre-fill rolling buffers with the baseline averages so the
  // average starts at a neutral value, not at zero
  initRollingBuffers(bl.avgMQ3, bl.avgMQ135);

  float peakRollingMQ3   = 0.0;   // highest rolling-avg seen in the window
  float peakRollingMQ135 = 0.0;
  int   totalSamples     = BREATH_DURATION_S * SAMPLE_HZ;
  int   lastSecond       = BREATH_DURATION_S;

  for (int s = 0; s < totalSamples; s++) {
    int rawMQ3   = analogRead(MQ3_PIN);
    int rawMQ135 = analogRead(MQ135_PIN);

    // ── Update circular buffers ───────────────────────────
    //  rollingIndex always points to the slot about to be overwritten
    //  (i.e. the oldest value). We replace it with the newest reading,
    //  then advance the index.
    rollingMQ3[rollingIndex]   = rawMQ3;
    rollingMQ135[rollingIndex] = rawMQ135;
    rollingIndex = (rollingIndex + 1) % ROLLING_WINDOW;

    // ── Compute rolling averages ──────────────────────────
    float sumMQ3 = 0, sumMQ135 = 0;
    for (int i = 0; i < ROLLING_WINDOW; i++) {
      sumMQ3   += rollingMQ3[i];
      sumMQ135 += rollingMQ135[i];
    }
    float ravgMQ3   = sumMQ3   / ROLLING_WINDOW;
    float ravgMQ135 = sumMQ135 / ROLLING_WINDOW;

    // ── Track peak of rolling average ────────────────────
    if (ravgMQ3   > peakRollingMQ3)   peakRollingMQ3   = ravgMQ3;
    if (ravgMQ135 > peakRollingMQ135) peakRollingMQ135 = ravgMQ135;

    // ── Print once per second ─────────────────────────────
    if (s % SAMPLE_HZ == 0) {
      float vMQ3     = adcToVoltage(rawMQ3);
      float vMQ135   = adcToVoltage(rawMQ135);
      float deltaMQ3   = ravgMQ3   - bl.avgMQ3;
      float deltaMQ135 = ravgMQ135 - bl.avgMQ135;

      // Print raw readings + the smoothed rolling-average delta
      printSensorLineWithDelta("BREATH", lastSecond,
                               rawMQ3,   vMQ3,   deltaMQ3,
                               rawMQ135, vMQ135, deltaMQ135);
      lastSecond--;
    }

    delay(SAMPLE_INTERVAL_MS);
  }

  // ── Compare PEAK of rolling average to thresholds ────────
  bool mq3Rose   = peakRollingMQ3   >= bl.threshMQ3;
  bool mq135Rose = peakRollingMQ135 >= bl.threshMQ135;

  Serial.println(F(""));
  Serial.println(F("  ---- RESULT ----"));
  Serial.print(F("  Peak rolling-avg MQ3  : ADC=")); Serial.print(peakRollingMQ3,   1);
  Serial.print(F("  threshold=")); Serial.print(bl.threshMQ3, 1);
  Serial.println(mq3Rose ? F("  [ROSE]") : F("  [flat]"));

  Serial.print(F("  Peak rolling-avg MQ135: ADC=")); Serial.print(peakRollingMQ135, 1);
  Serial.print(F("  threshold=")); Serial.print(bl.threshMQ135, 1);
  Serial.println(mq135Rose ? F("  [ROSE]") : F("  [flat]"));
  Serial.println(F(""));

  // ── Verdict ──────────────────────────────────────────────
  if (mq3Rose && !mq135Rose) {
    printVerdict("PERFUME / AMBIENT VAPOUR",
                 "MQ3 rose but MQ135 stayed flat — no breath detected.",
                 "Alcohol source in surrounding air, NOT a breath.");
  }
  else if (mq3Rose && mq135Rose) {
    printVerdict("*** ALCOHOL DETECTED IN BREATH ***",
                 "MQ3 rose (alcohol vapour) AND MQ135 rose (breath confirmed).",
                 "Alcohol is present in the breath.");
  }
  else if (!mq3Rose && mq135Rose) {
    printVerdict("NORMAL BREATH — NO ALCOHOL",
                 "MQ135 confirms a breath. MQ3 did not rise above threshold.",
                 "No alcohol detected.");
  }
  else {
    printVerdict("NOTHING DETECTED",
                 "Neither sensor rose above its baseline threshold.",
                 "Please breathe closer to the sensors.");
  }
}

// ============================================================
//  PHASE 4 — PURGE  (30 s, sensor recovery)
// ============================================================
void doPurge() {
  printHeader("PHASE: PURGE (sensor recovery)");
  Serial.println(F("Sensors clearing. Please step away."));
  Serial.println(F(""));

  for (int t = PURGE_DURATION_S; t > 0; t--) {
    int   rawMQ3   = analogRead(MQ3_PIN);
    int   rawMQ135 = analogRead(MQ135_PIN);
    float vMQ3     = adcToVoltage(rawMQ3);
    float vMQ135   = adcToVoltage(rawMQ135);

    printSensorLine("PURGE", t, rawMQ3, vMQ3, rawMQ135, vMQ135);
    delay(1000);
  }

  Serial.println(F(""));
  Serial.println(F("Purge complete! Starting new cycle..."));
}

// ============================================================
//  ROLLING BUFFER HELPERS
// ============================================================

// Fill both buffers with a single value (used on startup to zero-fill)
void initRollingBuffers(float val) {
  for (int i = 0; i < ROLLING_WINDOW; i++) {
    rollingMQ3[i]   = val;
    rollingMQ135[i] = val;
  }
  rollingIndex = 0;
}

// Fill buffers with separate baseline values (used before breath test
// so the rolling average starts at baseline, not zero)
void initRollingBuffers(float valMQ3, float valMQ135) {
  for (int i = 0; i < ROLLING_WINDOW; i++) {
    rollingMQ3[i]   = valMQ3;
    rollingMQ135[i] = valMQ135;
  }
  rollingIndex = 0;
}

// ============================================================
//  HELPERS
// ============================================================

// Convert 10-bit ADC value to voltage (5V reference, 1023 max)
float adcToVoltage(float adc) {
  return adc * (5.0 / 1023.0);
}

// Print one sensor line (no delta) — used in warm-up, purge, baseline
void printSensorLine(const char* phase, int timeLeft,
                     int rawMQ3, float vMQ3,
                     int rawMQ135, float vMQ135) {
  Serial.print(F("["));
  Serial.print(phase);
  Serial.print(F("] T-"));
  if (timeLeft < 10) Serial.print(F("0"));
  Serial.print(timeLeft);
  Serial.print(F("s"));

  Serial.print(F("  |  MQ3:  ADC="));
  Serial.print(rawMQ3);
  Serial.print(F("  V="));
  Serial.print(vMQ3, 3);

  Serial.print(F("  |  MQ135: ADC="));
  Serial.print(rawMQ135);
  Serial.print(F("  V="));
  Serial.println(vMQ135, 3);
}

// Print one sensor line WITH deltas — used during breath test
void printSensorLineWithDelta(const char* phase, int timeLeft,
                              int rawMQ3,   float vMQ3,   float dMQ3,
                              int rawMQ135, float vMQ135, float dMQ135) {
  Serial.print(F("["));
  Serial.print(phase);
  Serial.print(F("] T-"));
  if (timeLeft < 10) Serial.print(F("0"));
  Serial.print(timeLeft);
  Serial.print(F("s"));

  Serial.print(F("  |  MQ3:  ADC="));
  Serial.print(rawMQ3);
  Serial.print(F("  V="));
  Serial.print(vMQ3, 3);
  Serial.print(F("  dADC="));
  Serial.print(dMQ3, 1);

  Serial.print(F("  |  MQ135: ADC="));
  Serial.print(rawMQ135);
  Serial.print(F("  V="));
  Serial.print(vMQ135, 3);
  Serial.print(F("  dADC="));
  Serial.println(dMQ135, 1);
}

void printVerdict(const char* title, const char* line1, const char* line2) {
  Serial.println(F("  ================================"));
  Serial.print(F("  >> "));
  Serial.println(title);
  Serial.print(F("     "));
  Serial.println(line1);
  Serial.print(F("     "));
  Serial.println(line2);
  Serial.println(F("  ================================"));
}

void printHeader(const char* title) {
  Serial.println(F(""));
  Serial.println(F("================================================"));
  Serial.println(title);
  Serial.println(F("================================================"));
}
