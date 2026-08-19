# NITEDRIVE

A polyphonic synthesiser plugin (VST3 / AU) built for the Soulwax *Nite Versions*
palette: hard-detuned unison saws, a self-oscillating ladder filter, and drive
stages before *and* after that filter, finished with a phaser and a Juno-style
ensemble.

It is a C++/JUCE instrument, not a preset pack — the oscillators, filter and
saturation are written from scratch and measured.

---

## Install (macOS)

Download the `nitedrive-macos-universal` artefact from the latest
[Actions run](../../actions), unzip it, then:

```bash
cp -R NITEDRIVE.vst3     ~/Library/Audio/Plug-Ins/VST3/
cp -R NITEDRIVE.component ~/Library/Audio/Plug-Ins/Components/
```

Restart Live and rescan (**Preferences → Plug-Ins → Rescan**). NITEDRIVE appears
under *Plug-Ins → VST3* (or *Audio Units*) as an instrument.

The binaries are ad-hoc signed, not notarised, so the first load is blocked by
Gatekeeper. Clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NITEDRIVE.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/NITEDRIVE.component
```

Live 11.3 and later run natively on Apple Silicon and will load the arm64 slice;
the build is universal, so an Intel Mac or a Rosetta host works too.

> **AU users:** Live caches Audio Unit scans aggressively. If the component does not
> show up, run `killall -9 AudioComponentRegistrar` and rescan.
>
> If macOS still refuses it, the ad-hoc signature did not survive the download. Re-sign
> in place and rescan:
>
> ```bash
> codesign --force --sign - ~/Library/Audio/Plug-Ins/Components/NITEDRIVE.component
> codesign --force --sign - ~/Library/Audio/Plug-Ins/VST3/NITEDRIVE.vst3
> ```
>
> You can confirm the AU is loadable the same way macOS does:
> `auval -v aumu Ntdr Ndrv`

## Build from source

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

JUCE 8.0.4 is fetched automatically. To build against a checkout you already have,
set `JUCE_PATH=/path/to/JUCE`. macOS builds default to a universal arm64 + x86_64
binary; override with `-DCMAKE_OSX_ARCHITECTURES=arm64` for an Apple-Silicon-only
build.

---

## The instrument

```
osc 1 (unison x7) ─┐
osc 2 (unison x7) ─┤
sub               ─┼─► pre-drive ─► ladder filter ─► post-drive ─► amp ─► pan ─┐
noise             ─┤                                                            │
ring mod          ─┘                                                            │
                                                                                ▼
                              phaser ─► ensemble ─► delay ─► pump ─► output stage
```

**Oscillators.** Two independent stacks of up to seven detuned copies each. The
detune spacing and the centre/side gain balance follow the Roland JP-8000 curves —
the spacing is deliberately irregular so the copies never line up into audible
beating, and the detune knob is heavily curved so the narrow settings where a
supersaw actually lives occupy most of its travel. Saw, pulse (with PWM),
triangle and sine, plus a sub oscillator, noise, ring modulation, and hard sync
on oscillator 2.

**Filter.** A zero-delay-feedback Moog ladder in LP24 / LP12 / BP24 / BP12 / HP24.
It self-oscillates, and it tracks the cutoff setting to within 0.01% when it does.
Its drive control saturates the loop the way the hardware does, thinning the bass
as resonance climbs.

**Drive.** Two stages per voice — one into the filter, one out of it — with five
curves (soft, tube, hard, fold, fuzz). This is where most of the character lives;
the filter drive is a third stage inside the loop itself.

**Modulation.** Two envelopes, two tempo-syncable LFOs, and a six-slot matrix over
ten sources and fifteen destinations.

**Effects.** Phaser (2–12 stages, resonant feedback), three-voice Juno-style
ensemble, tempo-synced stereo delay with a filtered feedback path, and a
transport-locked ducker for the sidechain pump.

### Presets

12 factory patches, exposed to the host as programs: Init, Nite Bass, Rave Stab,
Acid Drive, Supersaw Lead, Fuzz Chords, Sync Scream, Ring Metal, Juno Pad,
Pump Saws, Sub Thump, Noise Sweep.

### CPU

The **Quality** control sets voice oversampling: Eco (1x), High (2x, default) and
Ultra (4x). High is the right default — see the aliasing tables below.

`ctest` prints the cost of the full path (engine + oversampling + every effect) for
the heaviest patch the synth can play: 16 voices, 7x unison on *both* oscillators,
all sustaining at once — 224 simultaneous oscillators.

| Quality | % of one 2.8 GHz Xeon core |
|---------|---------------------------|
| Eco (1x)   | 30% |
| High (2x)  | 54% |
| Ultra (4x) | 104% |

Ultra exceeds real time at that load on this machine, so treat it as a bounce/render
setting rather than a tracking one. Apple Silicon runs this workload considerably
faster, and normal patches are nowhere near 224 oscillators — a 3-voice unison bass
on 8 voices is roughly a tenth of the figures above.

---

## Measured behaviour

Everything below is produced by the test suite (`ctest`) or by
`tools/spectrum.py`, and is re-checked on every CI run.

**Oscillator aliasing** — inharmonic energy relative to harmonic energy, at 48 kHz:

| waveform | 55 Hz | 220 Hz | 880 Hz | 2 kHz | 4 kHz | 8 kHz |
|----------|-------|--------|--------|-------|-------|-------|
| saw      | −92 | −86 | −82 | −82 | −80 | −87 |
| pulse    | −95 | −89 | −84 | −84 | −82 | −90 |
| triangle | −137 | −119 | −101 | −113 | −125 | −135 |
| sine     | −148 | −147 | −140 | −131 | −134 | −130 |

For comparison, a polyBLEP saw — the usual approach — measures −33 dB at 880 Hz on
the same harness, and a naive one −17 dB.

**Full voice chain**, one saw through the drive stages and the ladder, counting
only what survives decimation:

| drive | Eco (1x) | High (2x) | Ultra (4x) |
|-------|----------|-----------|------------|
| 1  | −44 | −54 | −62 |
| 4  | −48 | −59 | −67 |
| 12 | −50 | −61 | −68 |
| 30 | −51 | −61 | −67 |

The figure barely moves with drive, which is the point of the antiderivative
anti-aliasing in the drive stages — before it, drive 30 measured −42 dB at High.

**Filter tuning** — self-oscillation frequency against the cutoff setting, across
55 Hz to 10 kHz and drive 1 to 40: within **0.01%** everywhere.

---

## Three things worth knowing about the DSP

**Wavetables, not polyBLEP.** polyBLEP only buys about 16 dB over a naive ramp.
That is inaudible on one saw and very audible on fourteen, so the oscillators read
from band-limited mips instead — built by additive synthesis, indexed by phase
increment so the bank is sample-rate independent and shared by every voice.

**The drive stages were the bottleneck, not the oscillators.** At 2x oversampling
they measured −42 dB against the oscillators' −93 dB. First-order antiderivative
anti-aliasing gained 18 dB there and made the number independent of drive amount,
for no extra CPU — every shaper was chosen to have an antiderivative in elementary
arithmetic, which is why the soft clip is a cubic rather than a `tanh`.

**The same trick is wrong inside the filter.** Antiderivative anti-aliasing
evaluates a shaper half a sample late, and half a sample of delay inside a resonant
loop drags the pitch flat — measured at −12.8% at 10 kHz. The ladder's saturator is
therefore memoryless, using a unity-slope shaper with makeup gain applied outside
the loop so the loop gain, and with it the tuning, is untouched.

---

## Layout

```
Source/dsp/       engine — no JUCE dependency, testable standalone
Source/           plugin wrapper, parameters, presets, GUI
Tests/            offline verification suite
tools/spectrum.py spectral analysis used to produce the tables above
```

## Licence

GNU AGPLv3 — see `LICENSE`. JUCE 8 is dual-licensed under the AGPLv3 and a
commercial licence; this project uses the open-source path, so it inherits those
terms. Building it into a closed-source product requires a commercial JUCE
licence from [juce.com](https://juce.com/legal/juce-8-licence/).
