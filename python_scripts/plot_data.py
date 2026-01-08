import sys
import os
import csv
import numpy as np
import matplotlib.pyplot as plt

CAPTURE_DIR = "captures"
SAMPLE_RATE = 16000

def plot_capture_data(sample_rate, capture_num, filename):
    if not os.path.exists(filename):
        print(f"Capture file not found: {filename}")
        sys.exit(1)

    samples = []

    with open(filename, newline="") as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 2:
                continue
            samples.append(int(row[1]))

    samples = np.array(samples, dtype=np.int16)

    # Time axis in seconds
    t = np.arange(len(samples)) / sample_rate

    plt.figure(figsize=(16, 8))
    plt.plot(t, samples)
    plt.grid(True)
    plt.tight_layout()
    plt.xlabel("Time (seconds)")
    plt.ylabel("Amplitude (PCM)")
    plt.title(f"Capture {capture_num} at {sample_rate} Hz")

    

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 plot_data.py <capture_number> [<capture_number> ...]")
        sys.exit(1)

    # Plot all captures passed as arguments
    for capture_num in sys.argv[1:]:
        filename = os.path.join(CAPTURE_DIR, f"{capture_num}.csv")
        plot_capture_data(SAMPLE_RATE, capture_num, filename)

    plt.show()

