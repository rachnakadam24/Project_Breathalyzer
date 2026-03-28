/*
 * ============================================================
 *  Contactless Breathalyser — Looping 3-Phase Test
 * ============================================================
 *
 *  PHASE FLOW:
 *
 *    First power-on:
 *      WARMUP (20s) → BASELINE (20s) → DETECTION (20s) → COOLDOWN (30s)
 *                          ↑                                     │
 *                          └─────────── loops forever ───────────┘
 *
 *    After the first cycle, WARMUP is skipped.
 *    Each new cycle re-computes a fresh baseline from clean air.
 *
 *  SAMPLING:
 *    Internal: every 100ms (for smooth moving average + dV/dt)
 *    Printed:  every 1000ms (1 row per second to Serial)
 *
 *  OUTPUT FORMAT (CSV):
 *    timestamp_ms, raw_adc, voltage_v, smoothed_adc, phase, note
 *
 *  WIRING:
 *    MQ3 AOUT → A0
 * ============================================================
 */

// ── Pin ──────────────────────────────────────────────────────
const int MQ3_PIN = A0;

// ── Phase Durations ──────────────────────────────────────────
const unsigned long WARMUP_MS    = 20000UL;
const unsigned long BASELINE_MS  = 30000UL;
const unsigned long DETECTION_MS = 30000UL;
const unsigned long COOLDOWN_MS  = 30000UL;

// ── Timing ───────────────────────────────────────────────────
const unsigned long SAMPLE_MS = 100;    // internal sample rate (for smoothing + dvdt)
const unsigned long PRINT_MS  = 1000;   // how often to print a CSV row

// ── Prior calibration anchor (from mq3_samples.csv) ──────────
const float PRIOR_MEAN_ADC = 48.3;
const float PRIOR_STD_ADC  = 0.47;

// ── Detection Tuning ─────────────────────────────────────────
const float K_SIGMA        = 4.0;   // threshold = mean + K * std
const int   DEBOUNCE_COUNT = 5;     // consecutive fast-rising hits to confirm alcohol
const float DVDT_MIN_ADC   = 2.0;  // min ADC/s rise rate to count as a breath spike

// ── Moving Average ────────────────────────────────────────────
const int AVG_SIZE = 10;
int   avgBuf[AVG_SIZE];
int   avgIdx  = 0;
bool  avgFull = false;

// ── States ────────────────────────────────────────────────────
enum Phase { WARMUP, BASELINE, DETECTION, COOLDOWN };
Phase phase = WARMUP;

// ── Cycle counter ─────────────────────────────────────────────
int cycleCount = 0;   // increments each time BASELINE starts

// ── Timing ───────────────────────────────────────────────────
unsigned long phaseStart = 0;
unsigned long lastSample = 0;
unsigned long lastPrint  = 0;

// ── Baseline accumulation (reset each cycle) ──────────────────
float baselineSum   = 0;
float baselineSumSq = 0;
int   baselineCount = 0;
float liveMean      = 0;
float liveStd       = 0;
float threshold     = 0;

// ── Detection state (reset each cycle) ───────────────────────
int  hitCount     = 0;
bool alcoholResult = false;

// ── dV/dt ─────────────────────────────────────────────────────
float         prevSmoothed = 0;
unsigned long prevT        = 0;

// ── Last computed values (used by print gate) ─────────────────
int   lastRaw      = 0;
float lastSmoothed = 0;
float lastDvdt     = 0;
const char* lastNote = "";

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

const char* phaseName() {
  switch (phase) {
    case WARMUP:    return "WARMUP";
    case BASELINE:  return "BASELINE";
    case DETECTION: return "DETECTION";
    case COOLDOWN:  return "COOLDOWN";
    default:        return "UNKNOWN";
  }
}

void printRow(unsigned long ts, const char* note) {
  float voltage = lastRaw * (5.0 / 1023.0);
  Serial.print(ts);              Serial.print(",");
  Serial.print(lastRaw);         Serial.print(",");
  Serial.print(voltage, 4);      Serial.print(",");
  Serial.print(lastSmoothed, 2); Serial.print(",");
  Serial.print(phaseName());     Serial.print(",");
  Serial.println(note);
}

void resetBaseline() {
  baselineSum   = 0;
  baselineSumSq = 0;
  baselineCount = 0;
  liveMean      = 0;
  liveStd       = 0;
  threshold     = 0;
}

void resetDetection() {
  hitCount      = 0;
  alcoholResult = false;
}

void startBaseline(unsigned long now) {
  phase      = BASELINE;
  phaseStart = now;
  cycleCount++;
  resetBaseline();
  resetDetection();

  Serial.println("#");
  Serial.println("# ============================================");
  Serial.print("# CYCLE "); Serial.println(cycleCount);
  Serial.println("# PHASE: BASELINE (20s)");
  Serial.println("# Keep sensor in CLEAN AIR.");
  Serial.println("# Do NOT bring alcohol near sensor.");
  Serial.println("# ============================================");
  Serial.println("#");
}

void startDetection(unsigned long now) {
  phase      = DETECTION;
  phaseStart = now;
  resetDetection();

  Serial.println("#");
  Serial.println("# ── BASELINE RESULT ──────────────────────────");
  Serial.print("#   Live mean      : "); Serial.print(liveMean, 2); Serial.println(" ADC");
  Serial.print("#   Live std       : "); Serial.print(liveStd, 4);  Serial.println(" ADC");
  Serial.print("#   Threshold ("); Serial.print(K_SIGMA); Serial.print("σ) : ");
  Serial.print(threshold, 2); Serial.println(" ADC");
  if (abs(liveMean - PRIOR_MEAN_ADC) > 5.0) {
    Serial.println("#   WARNING: baseline drifted >5 ADC from prior.");
    Serial.println("#   Consider re-running calibration script.");
  } else {
    Serial.println("#   Baseline matches prior calibration. Good.");
  }
  Serial.println("# ─────────────────────────────────────────────");
  Serial.println("#");
  Serial.println("# ============================================");
  Serial.println("# PHASE: DETECTION (20s)");
  Serial.println("# >>> BLOW near the sensor now! <<<");
  Serial.println("# Blow steadily from ~5cm away.");
  Serial.println("# ============================================");
  Serial.println("#");
}

void startCooldown(unsigned long now) {
  phase      = COOLDOWN;
  phaseStart = now;

  if (!alcoholResult) {
    Serial.println("#");
    Serial.println("# RESULT: NO alcohol detected in this window.");
  }
  Serial.println("#");
  Serial.println("# ============================================");
  Serial.println("# PHASE: COOLDOWN (30s)");
  Serial.println("# Remove alcohol source. Sensor is clearing.");
  Serial.println("# Next baseline will start automatically.");
  Serial.println("# ============================================");
  Serial.println("#");
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  pinMode(MQ3_PIN, INPUT);

  Serial.println("# ============================================");
  Serial.println("# Contactless Breathalyser — Looping Test");
  Serial.println("# ============================================");
  Serial.println("#");
  Serial.print("# Prior calibration: mean="); Serial.print(PRIOR_MEAN_ADC);
  Serial.print(" ADC  std="); Serial.print(PRIOR_STD_ADC); Serial.println(" ADC");
  Serial.print("# K="); Serial.print(K_SIGMA);
  Serial.print("  DEBOUNCE="); Serial.print(DEBOUNCE_COUNT);
  Serial.print("  DVDT_MIN="); Serial.print(DVDT_MIN_ADC); Serial.println(" ADC/s");
  Serial.println("#");
  Serial.println("# PHASE: WARMUP (20s) — sensor heating up...");
  Serial.println("# (Warmup only runs once at power-on)");
  Serial.println("#");
  Serial.println("timestamp_ms,raw_adc,voltage_v,smoothed_adc,phase,note");

  unsigned long now = millis();
  phaseStart   = now;
  lastSample   = now;
  lastPrint    = now;
  prevT        = now;
  prevSmoothed = analogRead(MQ3_PIN);
}

// ── Main Loop ─────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ══════════════════════════════════════════════════════════
  //  INTERNAL SAMPLE GATE — runs every 100ms
  //  Updates smoothing buffer and dV/dt. Does NOT print.
  // ══════════════════════════════════════════════════════════
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    lastRaw       = analogRead(MQ3_PIN);
    lastSmoothed  = smoothADC(lastRaw);

    float dt  = (now - prevT) / 1000.0;
    lastDvdt  = (dt > 0) ? (lastSmoothed - prevSmoothed) / dt : 0;
    prevSmoothed = lastSmoothed;
    prevT        = now;

    unsigned long elapsed = now - phaseStart;

    // ── Per-phase logic (runs at 100ms, not tied to print) ──

    if (phase == WARMUP) {
      lastNote = "heating";
      if (elapsed >= WARMUP_MS) {
        startBaseline(now);
      }
    }

    else if (phase == BASELINE) {
      baselineSum   += lastSmoothed;
      baselineSumSq += lastSmoothed * lastSmoothed;
      baselineCount++;
      lastNote = "collecting";

      if (elapsed >= BASELINE_MS) {
        // Compute stats
        liveMean      = baselineSum / baselineCount;
        float variance = (baselineSumSq / baselineCount) - (liveMean * liveMean);
        liveStd       = sqrt(max(0.0f, variance));  // guard against float rounding
        threshold     = liveMean + K_SIGMA * liveStd;
        startDetection(now);
      }
    }

    else if (phase == DETECTION) {
      bool aboveThreshold = (lastSmoothed > threshold);
      bool risingFast     = (lastDvdt > DVDT_MIN_ADC);

      if (!alcoholResult) {
        if (aboveThreshold && risingFast) {
          hitCount++;
          lastNote = "RISING";
        } else if (aboveThreshold) {
          hitCount = max(0, hitCount - 1);
          lastNote = "above-slow";
        } else {
          hitCount = 0;
          lastNote = "clear";
        }

        if (hitCount >= DEBOUNCE_COUNT) {
          alcoholResult = true;
          Serial.println("#");
          Serial.println("# !!! ALCOHOL DETECTED !!!");
          Serial.print("# Confirmed at t="); Serial.print(now); Serial.println("ms");
          Serial.println("#");
        }
      } else {
        lastNote = "ALCOHOL-CONFIRMED";
      }

      // 10s reminder
      if (elapsed >= DETECTION_MS / 2 &&
          elapsed <  DETECTION_MS / 2 + SAMPLE_MS &&
          !alcoholResult) {
        Serial.println("# Reminder: 10s left — blow near the sensor now!");
      }

      if (elapsed >= DETECTION_MS) {
        startCooldown(now);
      }
    }

    else if (phase == COOLDOWN) {
      lastNote = "cooling";
      if (elapsed >= COOLDOWN_MS) {
        // ── Loop back to BASELINE (no warmup after first cycle) ──
        Serial.println("#");
        Serial.println("# ============================================");
        Serial.print("# CYCLE "); Serial.print(cycleCount); Serial.println(" COMPLETE");
        Serial.print("# RESULT: ");
        Serial.println(alcoholResult ? "ALCOHOL DETECTED" : "NO ALCOHOL DETECTED");
        Serial.println("# Starting next cycle...");
        Serial.println("# ============================================");
        Serial.println("#");
        startBaseline(now);
      }
    }
  }

  // ══════════════════════════════════════════════════════════
  //  PRINT GATE — prints one CSV row per second
  // ══════════════════════════════════════════════════════════
  if (now - lastPrint >= PRINT_MS) {
    lastPrint = now;

    // Build note with countdown for cleaner output
    char noteBuf[40];
    unsigned long elapsed = now - phaseStart;

    if (phase == WARMUP) {
      int s = (WARMUP_MS    - min(elapsed, WARMUP_MS))    / 1000;
      sprintf(noteBuf, "heating %ds left", s);
    } else if (phase == BASELINE) {
      int s = (BASELINE_MS  - min(elapsed, BASELINE_MS))  / 1000;
      sprintf(noteBuf, "collecting %ds left", s);
    } else if (phase == DETECTION) {
      int s = (DETECTION_MS - min(elapsed, DETECTION_MS)) / 1000;
      if (alcoholResult) {
        sprintf(noteBuf, "ALCOHOL-CONFIRMED %ds left", s);
      } else {
        sprintf(noteBuf, "%s %ds left", lastNote, s);
      }
    } else if (phase == COOLDOWN) {
      int s = (COOLDOWN_MS  - min(elapsed, COOLDOWN_MS))  / 1000;
      sprintf(noteBuf, "cooling %ds left", s);
    }

    printRow(now, noteBuf);
  }
}
