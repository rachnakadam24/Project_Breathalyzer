"""
calibrate.py
------------
Reads MQ3 calibration data from Arduino over Serial,
saves raw CSV, then computes per-phase statistics.

Usage:
    pip install pyserial pandas
    python calibrate.py --port COM3        # Windows
    python calibrate.py --port /dev/ttyUSB0  # Linux/Mac
"""

import argparse
import csv
import time
from datetime import datetime
from pathlib import Path

import serial
import pandas as pd


# ── Config ──────────────────────────────────────────────────────────────────
BAUD_RATE    = 115200
TIMEOUT_S    = 2
K_SIGMA      = 2.0   # threshold = mean + K * std  (2σ → 95% coverage)
# ─────────────────────────────────────────────────────────────────────────────


def read_serial(port: str, output_csv: Path) -> None:
    print(f"[INFO] Connecting to {port} @ {BAUD_RATE} baud …")
    ser = serial.Serial(port, BAUD_RATE, timeout=TIMEOUT_S)
    time.sleep(2)  # allow Arduino reset

    rows_written = 0
    with open(output_csv, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["timestamp_ms", "raw_adc", "voltage", "phase"])

        print("[INFO] Receiving data (Ctrl+C to stop early) …\n")
        try:
            while True:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                if line.startswith("#"):          # progress comment
                    print(f"  {line}")
                    if "done" in line:
                        break
                    continue

                if line.startswith("timestamp"):  # duplicate header from Arduino
                    continue

                parts = line.split(",")
                if len(parts) == 4:
                    writer.writerow(parts)
                    f.flush()
                    rows_written += 1
                    print(f"  sample {rows_written:>4}: {parts}", end="\r")

        except KeyboardInterrupt:
            print("\n[WARN] Interrupted by user.")

    ser.close()
    print(f"\n[INFO] Saved {rows_written} rows → {output_csv}")


def analyse(csv_path: Path, k: float = K_SIGMA) -> dict:
    df = pd.read_csv(csv_path)
    df["raw_adc"] = pd.to_numeric(df["raw_adc"], errors="coerce")
    df["voltage"] = pd.to_numeric(df["voltage"], errors="coerce")
    df.dropna(inplace=True)

    results = {}
    print("\n" + "=" * 55)
    print(f"{'PHASE':<12} {'COUNT':>6} {'MEAN_V':>8} {'STD_V':>8} {'MIN_V':>8} {'MAX_V':>8}")
    print("=" * 55)

    for phase, grp in df.groupby("phase"):
        mean_v = grp["voltage"].mean()
        std_v  = grp["voltage"].std()
        min_v  = grp["voltage"].min()
        max_v  = grp["voltage"].max()
        results[phase] = dict(mean=mean_v, std=std_v, min=min_v, max=max_v, count=len(grp))
        print(f"{phase:<12} {len(grp):>6} {mean_v:>8.4f} {std_v:>8.4f} {min_v:>8.4f} {max_v:>8.4f}")

    print("=" * 55)

    # ── Dynamic Threshold ───────────────────────────────────────────────────
    if "baseline" in results:
        b = results["baseline"]
        threshold = b["mean"] + k * b["std"]
        print(f"\n[THRESHOLD]  mean + {k}σ  =  {b['mean']:.4f} + {k}×{b['std']:.4f}  =  {threshold:.4f} V")

        if "alcohol" in results:
            sep = results["alcohol"]["mean"] - threshold
            print(f"[SEPARATION] alcohol_mean − threshold  =  {sep:+.4f} V  "
                  f"({'✓ detectable' if sep > 0 else '✗ too close — increase K or sensor distance'})")

        # Save thresholds to a config file
        config_path = csv_path.parent / "calibration_config.txt"
        with open(config_path, "w") as cf:
            cf.write(f"# Generated: {datetime.now().isoformat()}\n")
            cf.write(f"# Source CSV: {csv_path.name}\n\n")
            cf.write(f"baseline_mean_v   = {b['mean']:.6f}\n")
            cf.write(f"baseline_std_v    = {b['std']:.6f}\n")
            cf.write(f"k_sigma           = {k}\n")
            cf.write(f"threshold_v       = {threshold:.6f}\n")
        print(f"\n[INFO] Calibration config saved → {config_path}")

    return results


def main():
    parser = argparse.ArgumentParser(description="MQ3 Breathalyser Calibration Tool")
    parser.add_argument("--port",    required=True,           help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--output",  default="mq3_samples.csv", help="Output CSV filename")
    parser.add_argument("--k",       type=float, default=K_SIGMA, help=f"Sigma multiplier (default {K_SIGMA})")
    parser.add_argument("--analyse-only", metavar="CSV",      help="Skip serial, analyse existing CSV")
    args = parser.parse_args()

    if args.analyse_only:
        analyse(Path(args.analyse_only), k=args.k)
    else:
        out = Path(args.output)
        read_serial(args.port, out)
        analyse(out, k=args.k)


if __name__ == "__main__":
    main()