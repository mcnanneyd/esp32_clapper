import serial
import time
import csv
import os
from datetime import datetime

SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE = 921600
OUTPUT_DIR = "pattern_logs"
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
print("Waiting for double or triple clap patterns...")
time.sleep(2)

if LOG_TO_FILE:
    csv_file = open(output_path, "w", newline="")
    writer = csv.writer(csv_file)
    writer.writerow(["type", "clap_count", "pattern_type", "duration_ms", "rms_avg", 
                     "rms_min", "rms_max", "zcr_avg", "gaps", "time_sec",
                     "noise_floor", "dynamic_threshold", "rms_stdev", "zcr_stdev"])

start_time = time.time()

try:
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if not line:
            continue

        parts = line.split(",")
        
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
                    writer.writerow(["STATS", "", "", "", rms_avg, rms_min, rms_max, 
                                    zcr_avg, "", f"{t:.4f}", noise_floor, 
                                    dynamic_threshold, rms_stdev, zcr_stdev])
                
                if PRINT_STATUS:
                    print(f"RMS: {rms_avg:7.1f}±{rms_stdev:6.1f} [{rms_min:6.1f}-{rms_max:6.1f}]  "
                          f"ZCR: {zcr_avg:5.1f}±{zcr_stdev:5.1f} [{zcr_min:3d}-{zcr_max:3d}]  "
                          f"Threshold: {dynamic_threshold:6.1f}", end="\r")
            except (ValueError, IndexError):
                continue
            continue
        
        if parts[0] == "PATTERN" and len(parts) >= 8:
            try:
                clap_count = int(parts[1])
                pattern_type = parts[2]     # {SHORT, LONG, MIXED}
                duration_ms = int(parts[3])
                rms_avg = float(parts[4])
                rms_min = float(parts[5])
                rms_max = float(parts[6])
                zcr_avg = int(parts[7])
                gaps = parts[8] if len(parts) > 8 else ""
                
                t = time.time() - start_time
                
                if LOG_TO_FILE:
                    writer.writerow(["PATTERN", clap_count, pattern_type, duration_ms, 
                                    rms_avg, rms_min, rms_max, zcr_avg, gaps, f"{t:.4f}",
                                    "", "", "", ""])
                    csv_file.flush()
                
                if PRINT_STATUS:
                    gap_list = gaps.split(":")
                    gap_str = ", ".join([f"{g}ms" for g in gap_list])
                    
                    clap_type = "DOUBLE" if clap_count == 2 else "TRIPLE" if clap_count == 3 else f"{clap_count}-CLAP"
                    
                    print(f"\n🎵 [{clap_type} - {pattern_type}] t={t:.3f}s  "
                          f"Duration={duration_ms}ms  Gaps=[{gap_str}]  "
                          f"RMS={rms_avg:.1f} [{rms_min:.1f}-{rms_max:.1f}]  "
                          f"ZCR={zcr_avg}")
                    
            except (ValueError, IndexError) as e:
                print(f"Error parsing pattern: {e}, parts: {parts}")
                continue
            continue

except KeyboardInterrupt:
    print("\n\nStopping.")

finally:
    if LOG_TO_FILE:
        csv_file.close()
    ser.close()
