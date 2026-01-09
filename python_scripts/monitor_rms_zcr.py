import serial
import time
import csv
import os
from datetime import datetime

SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE = 921600
OUTPUT_DIR = "rms_zcr_logs"
PRINT_STATUS = True
LOG_TO_FILE = True

def next_filename():
    n = 1
    while os.path.exists(f"{OUTPUT_DIR}/{n}.csv"):
        n += 1
    return f"{OUTPUT_DIR}/{n}.csv"

os.makedirs(OUTPUT_DIR, exist_ok=True)

timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
output_path = next_filename()

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)

print(f"Listening on {SERIAL_PORT} @ {BAUD_RATE} baud...")
time.sleep(2)

if LOG_TO_FILE:
    csv_file = open(output_path, "w", newline="")
    writer = csv.writer(csv_file)
    writer.writerow(["type", "rms", "zcr", "noise_floor", "trigger", "time_sec", "rms_stdev", "rms_min", "rms_max", "zcr_stdev", "zcr_min", "zcr_max", "dynamic_threshold"])

start_time = time.time()
last_trigger = 0
last_trigger_time = 0

try:
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if not line:
            continue

        parts = line.split(",")
        
        # Check if this is a statistics message
        if parts[0] == "STATS" and len(parts) == 11:
            try:
                rms_avg = float(parts[1])
                rms_stdev = float(parts[2])
                rms_min = float(parts[3])
                rms_max = float(parts[4])
                zcr_avg = float(parts[5])
                zcr_stdev = float(parts[6])
                zcr_min = int(parts[7])
                zcr_max = int(parts[8])
                noise_floor = float(parts[9])
                dynamic_threshold = float(parts[10])
                
                t = time.time() - start_time
                
                if LOG_TO_FILE:
                    writer.writerow(["STATS", rms_avg, zcr_avg, noise_floor, "", f"{t:.4f}", 
                                    rms_stdev, rms_min, rms_max, zcr_stdev, zcr_min, zcr_max, dynamic_threshold])
                
                if PRINT_STATUS:
                    print(f"RMS: {rms_avg:7.1f}±{rms_stdev:6.1f} [{rms_min:6.1f}-{rms_max:6.1f}]  ZCR: {zcr_avg:5.1f}±{zcr_stdev:5.1f} [{zcr_min:3d}-{zcr_max:3d}]  Threshold: {dynamic_threshold:6.1f}", end="\r")
            except ValueError:
                continue
            continue
        
        # Handle normal data messages
        if len(parts) != 4 and len(parts) != 5:
            continue

        try:
            rms = float(parts[0])
            zcr = int(parts[1])
            noise = float(parts[2])
            trigger = int(parts[3])
            timestamp_ms = int(parts[4]) if len(parts) == 5 else 0
        except ValueError:
            continue

        t = time.time() - start_time

        if LOG_TO_FILE:
            writer.writerow(["DATA", rms, zcr, noise, trigger, f"{t:.4f}", "", "", "", "", "", "", ""])

        if PRINT_STATUS:
            if trigger:
                # Print every trigger, but track timing
                current_trigger_time = time.time()

                print(f"\n[CLAP] t={t:.3f}s  RMS={rms:.1f}  ZCR={zcr}  ts={timestamp_ms}ms")
                last_trigger_time = current_trigger_time

        last_trigger = trigger

except KeyboardInterrupt:
    print("\nStopping.")

finally:
    if LOG_TO_FILE:
        csv_file.close()
    ser.close()
