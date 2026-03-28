import serial
import csv
from datetime import datetime
import time

PORT = "COM13"           # Change to your port
BAUD_RATE = 9600
OUTPUT_FILE = "mq135_readings.csv"

def main():
    print(f"Connecting to {PORT}...")
    ser = serial.Serial(PORT, BAUD_RATE, timeout=2)
    time.sleep(2)

    rows = []
    print("Listening... waiting for Arduino to finish.\n")

    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            print(line)

            # FIXED: explicit stop signal
            if line == "DONE":
                print("\nArduino finished. Saving CSV...")
                break

            if line.startswith("STATUS"):
                continue

            parts = line.split(",")
            if len(parts) == 3:
                label, index, value = parts
                timestamp = datetime.now().strftime("%H:%M:%S")
                rows.append([timestamp, label, index, value])

    except KeyboardInterrupt:
        print("\nStopped manually.")
    finally:
        ser.close()

    if rows:
        with open(OUTPUT_FILE, "w+", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["Timestamp", "Type", "Reading #", "Sensor Value"])
            writer.writerows(rows)
        print(f"Saved {len(rows)} readings to '{OUTPUT_FILE}'")
    else:
        print("No data collected.")

if __name__ == "__main__":
    main()