# DSP verification

`nitedrive_tests` is a measurement suite, not a smoke test. Each check renders audio
and puts a number on it, so a regression shows up as a number moving rather than as
a crash.

```bash
ctest --test-dir build --output-on-failure     # or run the binary directly
```

## What is covered

| Area | Check |
|------|-------|
| Wave tables | Bank size is sane; every mip stays under Nyquist across the note range |
| Oscillators | Aliasing per waveform at 55 Hz – 8 kHz, FFT-measured, must stay under −72 dB |
| Filter | Self-oscillation tracks the cutoff within 1% at drive 1, 4, 12 and 40 |
| Filter | All five modes survive a hot sweep at maximum resonance without blowing up |
| Envelope | Attack, decay and release timings; lands on exactly zero so voices free |
| Engine | Finite output, real signal level, DC blocked, bounded peak |
| Engine | Voices free after release; tail decays to exactly zero |
| Engine | Voice stealing with 32 notes on 4 voices stays finite |
| Engine | Mono and legato release cleanly after overlapping notes |
| Presets | Every preset id exists in the layout and every value survives loading |
| State | Parameters and program index round-trip through the host state blob |
| State | Foreign state data is rejected without corrupting the patch |
| Presets | Every factory patch renders without NaN and at a usable level |

## Why the thresholds are where they are

The oscillator limit is −72 dB while the measured worst case is −80 dB. The gap
absorbs platform float differences without letting a real regression through — the
polyBLEP implementation this replaced measured −33 dB, so anything approaching that
trips the test immediately.

The filter tuning check exists because it caught a genuine bug: antiderivative
anti-aliasing inside the ladder's feedback loop dragged self-oscillation 12.8% flat
at 10 kHz. Nothing else in the suite would have noticed.

## The plugin wrapper

The suite above covers the DSP and the parameter/preset/state contract. The wrapper
itself — parameter threading, editor lifecycle, bus layouts, odd block sizes and
sample rates — is covered by [pluginval](https://github.com/Tracktion/pluginval),
which CI runs at strictness level 10:

```bash
pluginval --strictness-level 10 --validate-in-process --validate build/NITEDRIVE_artefacts/Release/VST3/NITEDRIVE.vst3
```

## Measuring by hand

`tools/spectrum.py` implements the same analysis in Python for interactive work:

```bash
python3 tools/spectrum.py <fundamental-hz> <sample-rate> <raw-float32-file>
```

Two things matter when using it. Snap the test frequency to an exact FFT bin
(`round(f / (sr/N)) * (sr/N)`), or window leakage from a few hundred harmonics reads
as roughly 40 dB of aliasing that is not there. And pass `fmax` when analysing an
oversampled signal, since only content below the base Nyquist survives decimation.
