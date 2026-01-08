import serial
import struct
import os
import csv
from datetime import datetime

PORT = "/dev/ttyUSB0"
BAUD_RATE = 921600
OUTPUT_DIR = "captures"

os.makedirs(OUTPUT_DIR, exist_ok=True)

def next_filename():
    n = 1
    while os.path.exists(f"{OUTPUT_DIR}/{n}.csv"):
        n += 1
    return f"{OUTPUT_DIR}/{n}.csv"

ser = serial.Serial(PORT, BAUD_RATE, timeout=1)

print(f"Waiting for audio over {PORT} at {BAUD_RATE} baud...")

while True:
    if ser.read(4) != b"DUMP":
        continue

    print(f"{datetime.now().isoformat()}: Receiving capture...")
    
    count = struct.unpack("<I", ser.read(4))[0]
    raw = ser.read(count * 2)

    samples = struct.unpack(f"<{count}h", raw)

    filename = next_filename()
    with open(filename, "w", newline="") as f:
        writer = csv.writer(f)
        for i, s in enumerate(samples):
            writer.writerow([i, s])

    print(f"{datetime.now().isoformat()}: Saved {filename}")
