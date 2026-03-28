#define MQ135pin A1

float sensorValue;
float perfumeReadings[15];  // 15 readings over 30s (one every 2s)
float breathReadings[15];
int readingIndex = 0;
bool measuringPerfume = true;
unsigned long windowStart = 0;

void setup() {
  Serial.begin(9600);
  Serial.println("MQ135 warming up!");
  delay(20000);
  
  Serial.println("Starting perfume measurement window...");
  windowStart = millis();
}

void loop() {
  unsigned long elapsed = millis() - windowStart;

  // Switch windows every 30 seconds
  if (elapsed >= 30000) {
    if (measuringPerfume) {
      // Done collecting perfume data — display results
      Serial.println("\n--- Perfume Readings (30s) ---");
      for (int i = 0; i < readingIndex; i++) {
        Serial.print("  Reading ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(perfumeReadings[i]);
      }

      Serial.println("\nStarting breath measurement window...");
      measuringPerfume = false;
    } else {
      // Done collecting breath data — display results
      Serial.println("\n--- Breath Readings (30s) ---");
      for (int i = 0; i < readingIndex; i++) {
        Serial.print("  Reading ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(breathReadings[i]);
      }

      Serial.println("\nStarting perfume measurement window...");
      measuringPerfume = true;
    }

    // Reset for next window
    readingIndex = 0;
    windowStart = millis();
    delay(2000);
    return;
  }

  // Collect reading every 2 seconds
  sensorValue = analogRead(MQ135pin);

  if (measuringPerfume) {
    Serial.print("[PERFUME] Sensor Value: ");
    perfumeReadings[readingIndex] = sensorValue;
  } else {
    Serial.print("[BREATH] Sensor Value: ");
    breathReadings[readingIndex] = sensorValue;
  }

  Serial.println(sensorValue);
  readingIndex++;

  delay(2000);
}