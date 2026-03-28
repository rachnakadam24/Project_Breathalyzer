/*
 * ============================================================
 *  MQ3 + MQ135 — Hardware Debug Sketch
 *  Upload this FIRST to verify both sensors are wired and
 *  reading correctly before running the full fusion sketch.
 *
 *  Expected output in clean air:
 *    MQ3   : ~48–120 ADC  (0.23–0.58V)  varies by burn-in
 *    MQ135 : ~60–200 ADC  (0.29–0.97V)  varies by air quality
 *
 *  If you see 0 or 1023 constantly → wiring problem on that sensor.
 *  If you see nothing at all       → baud rate mismatch (set to 9600).
 *
 *  Wiring reminder:
 *    MQ3   AOUT → A0,  VCC → 5V,  GND → GND
 *    MQ135 AOUT → A1,  VCC → 5V,  GND → GND
 * ============================================================
 */

const int MQ3_PIN   = A0;
const int MQ135_PIN = A1;

void setup() {
  Serial.begin(9600);

  // Wait for Serial to connect (important on Leonardo/Micro, harmless on Uno)
  while (!Serial) { delay(10); }

  Serial.println("# ============================================");
  Serial.println("# MQ3 + MQ135 Hardware Debug");
  Serial.println("# Baud: 9600  |  Sampling: every 1s");
  Serial.println("# ============================================");
  Serial.println("timestamp_ms,mq3_raw,mq3_voltage,mq135_raw,mq135_voltage,status");
}

void loop() {
  int   mq3Raw   = analogRead(MQ3_PIN);
  int   mq135Raw = analogRead(MQ135_PIN);
  float mq3V     = mq3Raw   * (5.0 / 1023.0);
  float mq135V   = mq135Raw * (5.0 / 1023.0);

  // Simple sanity status
  const char* mq3Status;
  if      (mq3Raw == 0)    mq3Status = "MQ3_OPEN_CIRCUIT";
  else if (mq3Raw >= 1020) mq3Status = "MQ3_SHORT_OR_NO_GND";
  else                     mq3Status = "MQ3_OK";

  const char* mq135Status;
  if      (mq135Raw == 0)    mq135Status = "MQ135_OPEN_CIRCUIT";
  else if (mq135Raw >= 1020) mq135Status = "MQ135_SHORT_OR_NO_GND";
  else                       mq135Status = "MQ135_OK";

  Serial.print(millis());   Serial.print(",");
  Serial.print(mq3Raw);     Serial.print(",");
  Serial.print(mq3V, 4);    Serial.print(",");
  Serial.print(mq135Raw);   Serial.print(",");
  Serial.print(mq135V, 4);  Serial.print(",");

  // Combined status
  if (mq3Raw > 0 && mq3Raw < 1020 && mq135Raw > 0 && mq135Raw < 1020) {
    Serial.println("BOTH_OK");
  } else {
    Serial.print(mq3Status); Serial.print("|"); Serial.println(mq135Status);
  }

  delay(1000);
}
