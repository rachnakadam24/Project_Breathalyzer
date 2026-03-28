/*
 * Contactless Breathalyser - Phase Sampler
 * Phases: 0=Baseline, 1=Alcohol Present, 2=Cooldown
 * Sends CSV rows over Serial: timestamp_ms, raw_adc, voltage, phase
 */

const int MQ3_PIN     = A0;
const int LED_PIN     = 13;   // built-in LED blinks during sampling
const int SAMPLE_INTERVAL_MS = 100;  // sample every 100ms

// Phase durations (ms) — adjust as needed
const unsigned long WARMUP_MS   = 20000UL;  // 20s sensor warm-up
const unsigned long BASELINE_MS = 30000UL;  // 30s baseline
const unsigned long ALCOHOL_MS  = 30000UL;  // 30s alcohol exposure
const unsigned long COOLDOWN_MS = 30000UL;  // 30s cooldown

enum Phase { WARMUP, BASELINE, ALCOHOL, COOLDOWN, DONE };
Phase currentPhase = WARMUP;

unsigned long phaseStart    = 0;
unsigned long lastSample    = 0;
unsigned long sampleIndex   = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  Serial.println("# Contactless Breathalyser - Calibration Sampler");
  Serial.println("# Warming up MQ3 sensor (20s)...");
  Serial.println("timestamp_ms,raw_adc,voltage,phase");  // CSV header

  phaseStart = millis();
}

void loop() {
  unsigned long now = millis();
  unsigned long elapsed = now - phaseStart;

  // --- Phase transitions ---
  switch (currentPhase) {
    case WARMUP:
      if (elapsed >= WARMUP_MS) {
        currentPhase = BASELINE;
        phaseStart = now;
        Serial.println("# PHASE: baseline started");
      }
      break;

    case BASELINE:
      if (elapsed >= BASELINE_MS) {
        currentPhase = ALCOHOL;
        phaseStart = now;
        Serial.println("# PHASE: alcohol started — introduce alcohol source now");
      }
      break;

    case ALCOHOL:
      if (elapsed >= ALCOHOL_MS) {
        currentPhase = COOLDOWN;
        phaseStart = now;
        Serial.println("# PHASE: cooldown started — remove alcohol source");
      }
      break;

    case COOLDOWN:
      if (elapsed >= COOLDOWN_MS) {
        currentPhase = DONE;
        Serial.println("# PHASE: done — calibration complete");
      }
      break;

    case DONE:
      digitalWrite(LED_PIN, HIGH);  // steady LED = done
      return;
  }

  // --- Sampling (skip during warm-up) ---
  if (currentPhase != WARMUP && (now - lastSample >= SAMPLE_INTERVAL_MS)) {
    lastSample = now;

    int   raw     = analogRead(MQ3_PIN);
    float voltage = raw * (5.0 / 1023.0);

    // phase label
    const char* label = (currentPhase == BASELINE) ? "baseline"
                      : (currentPhase == ALCOHOL)  ? "alcohol"
                      :                              "cooldown";

    // CSV row
    Serial.print(now);       Serial.print(",");
    Serial.print(raw);       Serial.print(",");
    Serial.print(voltage, 4); Serial.print(",");
    Serial.println(label);

    // blink LED each sample
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);

    sampleIndex++;
  }
}