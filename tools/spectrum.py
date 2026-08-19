#!/usr/bin/env python3
"""Spectral analysis helpers for the NITEDRIVE DSP tests.

Reads raw float32 mono audio on stdin (or from a file) and reports how much of the
signal energy sits on the harmonic series of a known fundamental versus everywhere
else. For a band-limited oscillator, "everywhere else" is aliasing.
"""
import sys
import numpy as np


def load(path=None):
    raw = open(path, "rb").read() if path else sys.stdin.buffer.read()
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def blackman_harris(n):
    """4-term Blackman-Harris window (~-92 dB sidelobes).

    Written out rather than pulled from scipy so the test harness needs numpy only.
    """
    k = np.arange(n) / (n - 1)
    return (0.35875
            - 0.48829 * np.cos(2 * np.pi * k)
            + 0.14128 * np.cos(4 * np.pi * k)
            - 0.01168 * np.cos(6 * np.pi * k))


def alias_ratio_db(x, f0, sr, tol_bins=5, fmax=None):
    """Returns (inharmonic energy) / (harmonic energy) in dB.

    Lower is better. A naive (aliasing) saw lands around -12 dB at high pitches;
    a well band-limited one should be far below -60 dB.
    """
    n = len(x)
    win = blackman_harris(n)
    # Blackman-Harris has ~ -92 dB sidelobes, low enough not to masquerade as aliasing.
    spec = np.abs(np.fft.rfft(x * win)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    bin_hz = sr / n

    # Only content below fmax survives decimation, so only that counts as audible
    # aliasing. Above it, the downsampling filter removes everything.
    limit = min(sr / 2, fmax if fmax else sr / 2)

    harmonic = np.zeros(len(spec), dtype=bool)
    k = 1
    while k * f0 < limit:
        centre = int(round(k * f0 / bin_hz))
        lo, hi = max(0, centre - tol_bins), min(len(spec), centre + tol_bins + 1)
        harmonic[lo:hi] = True
        k += 1

    # Ignore DC and the few bins around it.
    dc_guard = int(round(20.0 / bin_hz)) + tol_bins
    harmonic[:dc_guard] = False
    mask = np.ones(len(spec), dtype=bool)
    mask[:dc_guard] = False
    mask[freqs > limit] = False
    harmonic[freqs > limit] = False

    h = spec[harmonic].sum()
    total = spec[mask].sum()
    inharm = max(total - h, 1e-30)
    return 10.0 * np.log10(inharm / max(h, 1e-30))


def peak_dbfs(x):
    return 20.0 * np.log10(max(np.max(np.abs(x)), 1e-12))


if __name__ == "__main__":
    f0 = float(sys.argv[1])
    sr = float(sys.argv[2])
    path = sys.argv[3] if len(sys.argv) > 3 else None
    x = load(path)
    if not np.all(np.isfinite(x)):
        print("NON_FINITE")
        sys.exit(1)
    print(f"{alias_ratio_db(x, f0, sr):.1f}")
