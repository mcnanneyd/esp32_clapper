import sys
import os
import csv
import numpy as np
import matplotlib.pyplot as plt
from typing import List, Tuple

CAPTURE_DIR = "captures"
SAMPLE_RATE = 16000

def plot_capture_data(sample_rate: int, capture_num: int, filename: str, show: bool=False) -> None:
    """
    Plot 

    Args:
        sample_rate (int): _description_
        capture_num (int): _description_
        filename (str): _description_
    """
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

    time_window_ms = 10  # 10 ms
    zcr_time, zcr = get_zcr_in_window(sample_rate, samples, time_window_ms)
    rms_time, rms = get_rms_in_window(sample_rate, samples, time_window_ms)

    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(16, 10), sharex=True)
    
    # Audio Signal
    ax1.plot(t, samples, label="Audio Signal")
    ax1.set_ylabel("Amplitude (PCM)")
    ax1.set_title(f"Capture {capture_num} at {sample_rate} Hz")
    ax1.legend()
    ax1.grid(True)
    
    # ZCR
    ax2.plot(zcr_time, zcr, label="ZCR", color="orange")
    ax2.set_ylabel("Zero Crossings")
    ax2.legend()
    ax2.grid(True)
    
    # RMS
    ax3.plot(rms_time, rms, label="RMS", color="green")
    ax3.set_ylabel("RMS")
    ax3.set_xlabel("Time (seconds)")
    ax3.legend()
    ax3.grid(True)
    
    plt.tight_layout()

    if show:
        plt.show()

def get_zcr_in_window(sample_rate: int, samples: np.array, time_window_ms: int) -> Tuple[List[int], List[int]]:
    zcr_in_window = int((time_window_ms / 1000) * sample_rate)
    zcr_values = []
    timestamps = []
    for i in range(0, len(samples), zcr_in_window):
        window = samples[i:i+zcr_in_window]
        if len(window) < 2:
            continue
        zero_crossings = np.sum(np.abs(np.diff(np.sign(window)))) // 2
        zcr_values.append(zero_crossings)
        timestamps.append(i / sample_rate)

    return timestamps, zcr_values

def get_rms_in_window(sample_rate: int, samples: np.array, time_window_ms: int) -> Tuple[List[int], List[float]]:
    rms_in_window = int((time_window_ms / 1000) * sample_rate)
    rms_values = []
    timestamps = []
    for i in range(0, len(samples), rms_in_window):
        window = samples[i:i+rms_in_window]
        if len(window) == 0:
            continue
        rms = np.sqrt(np.mean(window.astype(np.float32) ** 2))
        rms_values.append(rms)
        timestamps.append(i / sample_rate)

    return timestamps, rms_values

    

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 plot_data.py <capture_number> [<capture_number> ...]")
        sys.exit(1)

    # Plot all captures passed as arguments
    for capture_num in sys.argv[1:]:
        filename = os.path.join(CAPTURE_DIR, f"{capture_num}.csv")
        plot_capture_data(SAMPLE_RATE, capture_num, filename)

    plt.show()

