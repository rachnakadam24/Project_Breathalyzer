#define MQ135pin A1

float perfumeReadings[60];
float breathReadings[60];
int readingIndex = 0;
int state = 0;
unsigned long windowStart = 0;

void setup() {
  Serial.begin(9600);
  Serial.println("STATUS,Warming up...");
  delay(30000);
  Serial.println("STATUS,Starting perfume measurement...");
  windowStart = millis();
}

void loop() {
  unsigned long elapsed = millis() - windowStart;

  if (state == 0) {  // Perfume window
    if (elapsed >= 30000) {
      Serial.println("STATUS,Perfume done. Cooling down...");
      state = 1;
      readingIndex = 0;
      windowStart = millis();
      return;
    }
    if (readingIndex < 60) {
      float val = analogRead(MQ135pin);
      perfumeReadings[readingIndex] = val;
      Serial.print("PERFUME,");
      Serial.print(readingIndex + 1);
      Serial.print(",");
      Serial.println(val);
      readingIndex++;
    }
    delay(500);  // 2Hz = every 500ms
  }

  else if (state == 1) {  // Cooldown
    if (elapsed >= 120000) {
      Serial.println("STATUS,Cooldown done. Starting breath measurement...");
      state = 2;
      readingIndex = 0;
      windowStart = millis();
      return;
    }
    delay(1000);
  }

  else if (state == 2) {  // Breath window
    if (elapsed >= 30000) {
      Serial.println("STATUS,Breath done. All complete.");
      Serial.println("DONE");
      state = 3;
      return;
    }
    if (readingIndex < 60) {
      float val = analogRead(MQ135pin);
      breathReadings[readingIndex] = val;
      Serial.print("BREATH,");
      Serial.print(readingIndex + 1);
      Serial.print(",");
      Serial.println(val);
      readingIndex++;
    }
    delay(500);  // 2Hz = every 500ms
  }

  else if (state == 3) {
    delay(10000);
  }
}