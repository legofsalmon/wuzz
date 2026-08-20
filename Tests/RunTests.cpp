/*
    Offline DSP verification for NITEDRIVE.

    These are measurements, not smoke tests: each one renders audio and puts a number
    on it. The thresholds are set just loose enough to absorb platform float
    differences and tight enough that a real regression trips them.
*/

#include <JuceHeader.h>

#include <chrono>
#include <sstream>

#include "Params.h"
#include "Presets.h"
#include "StateIO.h"
#include "dsp/Effects.h"
#include "dsp/SynthEngine.h"

namespace
{

int failures = 0;
int checks = 0;

void report (bool ok, const juce::String& name, const juce::String& detail)
{
    ++checks;
    if (! ok)
        ++failures;

    std::cout << (ok ? "  pass  " : "  FAIL  ") << name.toStdString();

    if (detail.isNotEmpty())
        std::cout << "   [" << detail.toStdString() << "]";

    std::cout << std::endl;
}

void checkTrue (bool ok, const juce::String& name, const juce::String& detail = {})
{
    report (ok, name, detail);
}

void checkBelow (double value, double limit, const juce::String& name, const juce::String& unit = "dB")
{
    report (value <= limit,
            name,
            juce::String (value, 1) + " " + unit + " (limit " + juce::String (limit, 1) + ")");
}

void checkWithin (double value, double target, double tolerance,
                  const juce::String& name, const juce::String& unit = "%")
{
    report (std::abs (value - target) <= tolerance,
            name,
            juce::String (value, 3) + unit + " vs " + juce::String (target, 3)
                + unit + " +/-" + juce::String (tolerance, 3));
}

void section (const juce::String& title)
{
    std::cout << "\n== " << title.toStdString() << " ==" << std::endl;
}

// ---------------------------------------------------------------------------
// Spectral analysis. Mirrors tools/spectrum.py so the C++ suite and the Python
// harness report the same numbers.
// ---------------------------------------------------------------------------

// Matches tools/spectrum.py: same transform length and the same harmonic tolerance,
// so the C++ suite and the Python harness report the same number for the same
// signal. They disagreed by tens of dB at low pitches when the tolerance was a
// larger fraction of the harmonic spacing.
constexpr int kFftOrder = 16;
constexpr int kFftSize = 1 << kFftOrder;

std::vector<float> blackmanHarris (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double k = (double) i / (double) (n - 1);
        w[(size_t) i] = (float) (0.35875
                               - 0.48829 * std::cos (2.0 * juce::MathConstants<double>::pi * k)
                               + 0.14128 * std::cos (4.0 * juce::MathConstants<double>::pi * k)
                               - 0.01168 * std::cos (6.0 * juce::MathConstants<double>::pi * k));
    }
    return w;
}

/** Inharmonic energy relative to harmonic energy, in dB. Only content below fmax is
    counted, since a decimation filter removes the rest. */
double aliasRatioDb (const float* x, int n, double f0, double sr, double fmax, int tolBins = 5)
{
    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> buf ((size_t) kFftSize * 2, 0.0f);
    const auto win = blackmanHarris (juce::jmin (n, kFftSize));

    const int count = juce::jmin (n, kFftSize);
    for (int i = 0; i < count; ++i)
        buf[(size_t) i] = x[i] * win[(size_t) i];

    fft.performFrequencyOnlyForwardTransform (buf.data());

    const double binHz = sr / (double) kFftSize;
    const double limit = juce::jmin (sr * 0.5, fmax);
    const int numBins = kFftSize / 2;

    std::vector<bool> harmonic ((size_t) numBins, false);
    for (int k = 1; (double) k * f0 < limit; ++k)
    {
        const int centre = (int) std::lround ((double) k * f0 / binHz);
        for (int b = centre - tolBins; b <= centre + tolBins; ++b)
            if (b >= 0 && b < numBins)
                harmonic[(size_t) b] = true;
    }

    const int dcGuard = (int) std::lround (20.0 / binHz) + tolBins;
    double h = 0.0, total = 0.0;

    for (int b = dcGuard; b < numBins; ++b)
    {
        if ((double) b * binHz > limit)
            break;

        const double p = (double) buf[(size_t) b] * (double) buf[(size_t) b];
        total += p;
        if (harmonic[(size_t) b])
            h += p;
    }

    const double inharmonic = juce::jmax (total - h, 1.0e-30);
    return 10.0 * std::log10 (inharmonic / juce::jmax (h, 1.0e-30));
}

/** Snaps a frequency to an exact FFT bin so window leakage does not masquerade as
    aliasing. Without this every measurement here reads ~40 dB too pessimistic. */
double snapToBin (double freq, double sr)
{
    const double binHz = sr / (double) kFftSize;
    return std::round (freq / binHz) * binHz;
}

bool allFinite (const float* x, int n)
{
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (x[i]))
            return false;
    return true;
}

// ---------------------------------------------------------------------------

void testWaveTables()
{
    section ("Wave tables");

    const auto& w = nd::WaveTables::instance();

    checkTrue (w.memoryBytes() > 0 && w.memoryBytes() < 8u * 1024u * 1024u,
               "table bank size is sane",
               juce::String ((int) (w.memoryBytes() / 1024)) + " KB");

    // Mip selection must never pick a table with more harmonics than fit under
    // Nyquist, or the oscillator aliases outright.
    bool bandLimitOk = true;
    juce::String worst;

    for (double f = 20.0; f < 20000.0; f *= 1.02)
    {
        const float inc = (float) (f / 48000.0);
        const int idx = nd::WaveTables::indexForIncrement (inc);
        const auto& t = w.saw (idx);

        // Reconstruct the harmonic count this mip was built for.
        const float incHigh = nd::WaveTables::kIncBase
                            * std::exp2 ((float) (idx + 1) / (float) nd::WaveTables::kTablesPerOctave);
        const int harmonics = juce::jlimit (1, nd::WaveTables::kMaxHarmonics, (int) (0.5f / incHigh));

        if ((double) harmonics * f > 24000.0 * 1.02 || t.size < 16)
        {
            bandLimitOk = false;
            worst = juce::String (f, 0) + " Hz -> " + juce::String (harmonics) + " harmonics";
        }
    }

    checkTrue (bandLimitOk, "every mip stays under Nyquist across the note range", worst);
}

void testOscillatorAliasing()
{
    section ("Oscillator aliasing");

    const double sr = 48000.0;
    const char* names[] = { "saw", "pulse", "triangle", "sine" };

    // The wavetable oscillators measure -80 dB or better; the limit leaves room for
    // platform float differences without letting a real regression through.
    constexpr double limitDb = -72.0;

    // Printed as a markdown row so the README table is copied from a real run
    // rather than maintained by hand - it drifted out of step with the code before.
    std::cout << "\n  | waveform | 55 Hz | 220 Hz | 880 Hz | 2 kHz | 4 kHz | 8 kHz |" << std::endl;
    std::cout << "  |----------|-------|--------|--------|-------|-------|-------|" << std::endl;

    for (int wave = 0; wave < 4; ++wave)
    {
        double worst = -200.0;
        double worstFreq = 0.0;
        juce::String row = "  | " + juce::String (names[wave]).paddedRight (' ', 8) + " |";

        for (double target : { 55.0, 220.0, 880.0, 2000.0, 4000.0, 8000.0 })
        {
            const double f = snapToBin (target, sr);
            const float inc = (float) (f / sr);

            nd::Osc osc;
            osc.reset (0.0f);
            auto tables = nd::TableSet::forIncrement (inc);

            std::vector<float> buf ((size_t) kFftSize);
            for (int i = 0; i < kFftSize; ++i)
                buf[(size_t) i] = osc.process (tables, inc, (nd::Wave) wave, 0.35f);

            checkTrue (allFinite (buf.data(), kFftSize),
                       juce::String (names[wave]) + " output finite at " + juce::String (target, 0) + " Hz");

            const double r = aliasRatioDb (buf.data(), kFftSize, f, sr, sr * 0.5);
            if (r > worst) { worst = r; worstFreq = target; }

            row += " " + juce::String (juce::roundToInt (r)) + " |";
        }

        std::cout << row << std::endl;

        checkBelow (worst, limitDb,
                    juce::String (names[wave]) + " worst-case aliasing (at "
                        + juce::String (worstFreq, 0) + " Hz)");
    }
}

void testFilter()
{
    section ("Ladder filter");

    const double sr = 96000.0;

    // Self-oscillation must sit on the cutoff at every drive setting. Antiderivative
    // anti-aliasing in this loop used to drag it 12.8% flat; this test is what caught it.
    for (float drive : { 1.0f, 4.0f, 12.0f, 40.0f })
    {
        double worstErr = 0.0;
        double worstAt = 0.0;

        for (float fc : { 55.0f, 110.0f, 440.0f, 1000.0f, 4000.0f, 10000.0f })
        {
            nd::LadderFilter lf;
            lf.prepare (sr);
            lf.setMode (nd::FilterMode::LP24);

            const int n = (int) sr * 2;
            std::vector<float> b ((size_t) n);
            for (int i = 0; i < n; ++i)
                b[(size_t) i] = lf.process (i < 32 ? 0.5f : 0.0f, fc, 1.0f, drive);

            int crossings = 0;
            for (int i = n / 2 + 1; i < n; ++i)
                if ((b[(size_t) (i - 1)] < 0.0f) != (b[(size_t) i] < 0.0f))
                    ++crossings;

            const double measured = crossings * 0.5 * sr / (double) (n - n / 2 - 1);
            const double errPct = 100.0 * (measured / (double) fc - 1.0);

            if (std::abs (errPct) > std::abs (worstErr)) { worstErr = errPct; worstAt = fc; }
        }

        checkWithin (worstErr, 0.0, 1.0,
                     "self-oscillation tracks cutoff at drive " + juce::String (drive, 0)
                         + " (worst at " + juce::String (worstAt, 0) + " Hz)");
    }

    // Nothing may blow up, whatever the mode or the input.
    for (int mode = 0; mode < (int) nd::FilterMode::NumModes; ++mode)
    {
        nd::LadderFilter lf;
        lf.prepare (sr);
        lf.setMode ((nd::FilterMode) mode);

        bool finite = true;
        float peak = 0.0f;
        const int n = (int) sr * 2;

        for (int i = 0; i < n; ++i)
        {
            const float x = 4.0f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr);
            const float fc = 20.0f * std::pow (1000.0f, (float) i / (float) n);
            const float y = lf.process (x, fc, 1.0f, 40.0f);

            if (! std::isfinite (y))
                finite = false;

            peak = juce::jmax (peak, std::abs (y));
        }

        checkTrue (finite && peak < 100.0f,
                   "mode " + juce::String (mode) + " survives a hot sweep at max resonance",
                   "peak " + juce::String (peak, 2));
    }
}

/** Antiderivative anti-aliasing is only correct if F really is the antiderivative
    of f. If it is off by a constant factor the drive stage silently runs at the
    wrong level - and worse, disagrees with its own low-slope fallback branch, which
    evaluates f directly. A factor of 2*pi (16 dB) shipped in the Fold shaper before
    this test existed, and every level-based check passed straight through it. */
void testShaperAntiderivatives()
{
    section ("Drive shaper antiderivatives");

    const char* names[] = { "Soft", "Tube", "Hard", "Fold", "Fuzz" };

    for (int t = 0; t < (int) nd::DriveType::NumTypes; ++t)
    {
        const auto type = (nd::DriveType) t;
        double worst = 0.0, worstAt = 0.0;

        for (double x = -6.0; x <= 6.0; x += 0.001)
        {
            // The shapers have knees where f is not differentiable; a central
            // difference straddling one is meaningless, so skip those neighbourhoods.
            const bool nearKnee = std::abs (std::abs (x) - 1.0) < 0.02
                               || std::abs (std::abs (x + (double) nd::ShapeMath::kTubeBias) - 1.0) < 0.02;
            if (nearKnee)
                continue;

            const double h = 1.0e-4;
            const double dF = (nd::ShapeMath::F (type, (float) (x + h))
                             - nd::ShapeMath::F (type, (float) (x - h))) / (2.0 * h);
            const double f = nd::ShapeMath::f (type, (float) x);

            const double err = std::abs (dF - f);
            if (err > worst) { worst = err; worstAt = x; }
        }

        checkBelow (worst, 0.02,
                    juce::String (names[t]) + ": F is the antiderivative of f (worst at x="
                        + juce::String (worstAt, 2) + ")",
                    "");
    }
}

void testEnvelope()
{
    section ("Envelope");

    const double sr = 48000.0;
    nd::AdsrEnv env;
    env.prepare (sr);
    env.setParams (0.05f, 0.1f, 0.5f, 0.2f);
    env.noteOn();

    int attackSamples = 0;
    while (env.getLevel() < 0.99f && attackSamples < (int) sr)
    {
        env.process();
        ++attackSamples;
    }

    checkWithin (attackSamples / sr, 0.05, 0.015, "attack reaches full in the set time", "s");

    for (int i = 0; i < (int) (sr * 0.5); ++i)
        env.process();

    checkWithin (env.getLevel(), 0.5, 0.02, "decays to the sustain level", "");

    env.noteOff();
    int releaseSamples = 0;
    while (env.isActive() && releaseSamples < (int) sr * 4)
    {
        env.process();
        ++releaseSamples;
    }

    checkTrue (! env.isActive(), "release reaches idle so the voice can be freed");
    checkWithin (releaseSamples / sr, 0.2, 0.12, "release time is in range", "s");
    checkTrue (env.getLevel() == 0.0f, "envelope lands on exactly zero (no denormal tail)");
}

/** A mono-voiced patch must produce bit-identical channels at ANY engine rate.

    The right channel's filter and DC blocker ran with 48 kHz defaults for the
    project's whole life because every test rendered at exactly 48 kHz - the one
    rate where the mistuning is invisible. Three review agents found it
    independently. This test renders at rates where the bug screams. */
void testStereoSymmetry()
{
    section ("Stereo symmetry");

    for (double sr : { 88200.0, 96000.0, 192000.0 })
    {
        nd::SynthEngine engine;
        engine.prepare (sr, 4);

        nd::EngineParams p;
        p.osc1.unison = 1;
        p.osc1.spread = 0.0f;
        p.osc2.level = 0.0f;
        p.cutoff = 800.0f;
        p.resonance = 0.6f;
        p.filterDrive = 6.0f;
        p.filterEnv = 0.5f;
        p.preDrive = 4.0f;
        p.postDrive = 3.0f;
        p.ampA = 0.001f; p.ampS = 1.0f;
        p.velToAmp = 0.0f;
        p.analogDrift = 0.0f;
        p.randomPhase = false;

        const int n = (int) sr / 2;
        std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);

        engine.noteOn (45, 1.0f, p);
        engine.render (p, L.data(), R.data(), n);

        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
            worst = juce::jmax (worst, std::abs (L[(size_t) i] - R[(size_t) i]));

        double rms = 0.0;
        for (int i = 0; i < n; ++i)
            rms += (double) L[(size_t) i] * L[(size_t) i];
        rms = std::sqrt (rms / n);

        // Identical channels is also true of a broken, silent render - require the
        // patch to actually sound before the symmetry claim means anything.
        checkTrue (rms > 0.05, "symmetry patch actually renders sound at "
                        + juce::String (sr / 1000.0, 1) + " kHz",
                   "rms " + juce::String (rms, 4));
        checkBelow (worst, 1.0e-6, "mono patch renders identical channels at "
                        + juce::String (sr / 1000.0, 1) + " kHz", "");
    }
}

void testEngine()
{
    section ("Engine");

    const double base = 48000.0;
    const int os = 2;
    const double sr = base * os;

    nd::SynthEngine engine;
    engine.prepare (sr, 16);

    nd::EngineParams p;
    p.osc1.unison = 7; p.osc1.detune = 0.4f; p.osc1.spread = 0.8f;
    p.osc2.unison = 5; p.osc2.detune = 0.3f; p.osc2.level = 0.7f; p.osc2.octave = -1;
    p.subLevel = 0.4f; p.noiseLevel = 0.05f; p.ringLevel = 0.2f;
    p.cutoff = 1800.0f; p.resonance = 0.6f; p.filterDrive = 8.0f; p.filterEnv = 0.6f;
    p.preDrive = 6.0f; p.preDriveType = nd::DriveType::Tube;
    p.postDrive = 4.0f; p.postDriveType = nd::DriveType::Fuzz;
    p.ampA = 0.003f; p.ampD = 0.3f; p.ampS = 0.6f; p.ampR = 0.2f;
    p.mod[0] = { nd::ModSource::Lfo1, nd::ModDest::Cutoff, 0.4f };
    p.mod[1] = { nd::ModSource::Env2, nd::ModDest::PreDrive, 0.3f };

    const int n = (int) (sr * 3);
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);

    const int notes[] = { 36, 43, 48, 52, 55, 59, 64, 67 };
    for (int note : notes)
        engine.noteOn (note, 0.9f, p);

    int pos = 0;
    while (pos < n)
    {
        const int blockSize = juce::jmin (128, n - pos);

        if (pos >= (int) (sr * 1.5) && pos < (int) (sr * 1.5) + 128)
            for (int note : notes)
                engine.noteOff (note, p);

        engine.render (p, &L[(size_t) pos], &R[(size_t) pos], blockSize);
        pos += blockSize;
    }

    checkTrue (allFinite (L.data(), n) && allFinite (R.data(), n), "engine output stays finite");

    double dc = 0.0, rms = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        dc += L[(size_t) i];
        rms += (double) L[(size_t) i] * L[(size_t) i];
        peak = juce::jmax (peak, std::abs (L[(size_t) i]));
    }
    dc /= n;
    rms = std::sqrt (rms / n);

    checkTrue (rms > 0.01, "engine actually produces sound", "rms " + juce::String (rms, 4));
    checkBelow (std::abs (dc), 0.001, "dc offset is blocked", "");
    checkTrue (peak < 40.0f, "peak stays bounded", "peak " + juce::String (peak, 2));

    checkTrue (engine.getActiveVoiceCount() == 0, "all voices free after release");
    checkTrue (L[(size_t) (n - 1)] == 0.0f && R[(size_t) (n - 1)] == 0.0f,
               "tail decays to exactly zero");

    // Voice stealing: far more notes than voices, held down, must not misbehave.
    engine.allNotesOff();
    engine.setNumVoices (4);

    std::vector<float> sL ((size_t) (int) sr, 0.0f), sR ((size_t) (int) sr, 0.0f);
    for (int i = 0; i < 32; ++i)
    {
        engine.noteOn (40 + i, 0.8f, p);
        engine.render (p, &sL[(size_t) (i * 128)], &sR[(size_t) (i * 128)], 128);
    }

    checkTrue (allFinite (sL.data(), 32 * 128), "voice stealing stays finite");

    // Mono and legato must not leave a voice stuck on.
    for (auto mode : { nd::VoiceMode::Mono, nd::VoiceMode::Legato })
    {
        nd::SynthEngine mono;
        mono.prepare (sr, 8);
        nd::EngineParams mp = p;
        mp.voiceMode = mode;
        mp.glideTime = 0.05f;
        mp.ampR = 0.01f;

        std::vector<float> b ((size_t) 4096, 0.0f);

        mono.noteOn (48, 0.9f, mp);
        mono.render (mp, b.data(), b.data(), 2048);
        mono.noteOn (55, 0.9f, mp);
        mono.render (mp, b.data(), b.data(), 2048);
        mono.noteOff (55, mp);
        mono.render (mp, b.data(), b.data(), 2048);
        mono.noteOff (48, mp);

        for (int i = 0; i < 200; ++i)
            mono.render (mp, b.data(), b.data(), 2048);

        checkTrue (mono.getActiveVoiceCount() == 0,
                   juce::String (mode == nd::VoiceMode::Mono ? "mono" : "legato")
                       + " releases cleanly after overlapping notes");
    }
}

/** Dominant frequency by zero-crossing count. Only meaningful on a clean sine, which
    is what the glide test uses. */
double zeroCrossingFreq (const float* x, int n, double sr)
{
    int crossings = 0;
    for (int i = 1; i < n; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++crossings;

    return crossings * 0.5 * sr / (double) (n - 1);
}

void testGlide()
{
    section ("Glide");

    const double sr = 96000.0;

    // A clean sine with no filter movement, so pitch is the only thing being measured.
    nd::EngineParams base;
    base.osc1.wave = nd::Wave::Sine;
    base.osc1.unison = 1;
    base.osc1.level = 1.0f;
    base.osc2.level = 0.0f;
    base.cutoff = 20000.0f;
    base.filterDrive = 1.0f;
    base.filterEnv = 0.0f;
    base.ampA = 0.0002f; base.ampD = 0.01f; base.ampS = 1.0f; base.ampR = 0.01f;
    base.velToAmp = 0.0f;
    base.analogDrift = 0.0f;
    base.randomPhase = false;
    base.glideTime = 0.4f;

    const int lowNote = 45;    // 110 Hz
    const int highNote = 69;   // 440 Hz

    for (bool legatoOnly : { true, false })
    {
        nd::EngineParams p = base;
        p.glideLegatoOnly = legatoOnly;

        nd::SynthEngine engine;
        engine.prepare (sr, 8);

        std::vector<float> buf ((size_t) (int) sr, 0.0f);

        // Play the low note, let it settle, release it fully, then play the high one.
        engine.noteOn (lowNote, 1.0f, p);
        engine.render (p, buf.data(), buf.data(), (int) (sr * 0.3));
        engine.noteOff (lowNote, p);
        engine.render (p, buf.data(), buf.data(), (int) (sr * 0.2));

        std::vector<float> onset ((size_t) (int) (sr * 0.02), 0.0f);
        std::vector<float> dummy ((size_t) onset.size(), 0.0f);
        engine.noteOn (highNote, 1.0f, p);
        engine.render (p, onset.data(), dummy.data(), (int) onset.size());

        const double f = zeroCrossingFreq (onset.data(), (int) onset.size(), sr);

        if (legatoOnly)
        {
            checkWithin (f, 440.0, 40.0,
                         "legato-only glide starts a fresh note in tune", " Hz");
        }
        else
        {
            // "Starts low" alone is also true of a voice stuck at the old pitch, so
            // require it to actually arrive as well. render() sums into its output,
            // so the buffer has to be cleared or this measures every chunk at once.
            std::vector<float> settled ((size_t) (int) (sr * 0.02), 0.0f);

            for (int i = 0; i < 60; ++i)                       // ~1.2 s, three glide time constants
            {
                std::fill (settled.begin(), settled.end(), 0.0f);
                engine.render (p, settled.data(), dummy.data(), (int) settled.size());
            }

            const double arrived = zeroCrossingFreq (settled.data(), (int) settled.size(), sr);

            checkTrue (f < 260.0 && arrived > 380.0,
                       "always-glide slides up from the previous note and arrives",
                       juce::String (f, 1) + " Hz at onset -> " + juce::String (arrived, 1)
                           + " Hz settled, target 440 Hz");
        }
    }
}

/** Reports how much of one core the full signal path costs. Informational rather
    than pass/fail - the number is hardware specific, and a threshold here would just
    be a flaky test on shared CI runners. */
void reportPerformance()
{
    section ("Performance (informational)");

    const double base = 48000.0;

    for (int quality = 0; quality <= 2; ++quality)
    {
        const int factor = 1 << quality;
        const double sr = base * factor;

        nd::SynthEngine engine;
        engine.prepare (sr, 16);

        nd::EngineParams p;
        p.osc1.unison = 7; p.osc1.detune = 0.4f; p.osc1.spread = 0.8f;
        p.osc2.unison = 7; p.osc2.detune = 0.5f; p.osc2.level = 0.7f;
        p.subLevel = 0.4f;
        p.cutoff = 2000.0f; p.resonance = 0.6f; p.filterDrive = 8.0f;
        p.preDrive = 6.0f; p.postDrive = 3.0f;
        p.ampS = 1.0f; p.ampA = 0.001f;      // hold every voice open

        for (int i = 0; i < 16; ++i)
            engine.noteOn (36 + i * 2, 0.9f, p);

        nd::Phaser phaser; phaser.prepare (base);
        nd::Ensemble ensemble; ensemble.prepare (base);
        nd::StereoDelay delay; delay.prepare (base);
        nd::OutputStage out; out.prepare (base);

        juce::dsp::Oversampling<float> os (2, (size_t) juce::jmax (1, quality),
                                           juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                           true, true);
        os.initProcessing (512);

        juce::AudioBuffer<float> buffer (2, 512);
        const int blocks = (int) (base * 2.0 / 512.0);

        const auto start = std::chrono::high_resolution_clock::now();

        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::dsp::AudioBlock<float> block (buffer);

            if (quality > 0)
            {
                auto osBlock = os.processSamplesUp (block);
                engine.render (p, osBlock.getChannelPointer (0), osBlock.getChannelPointer (1),
                               512 * factor);
                os.processSamplesDown (block);
            }
            else
            {
                engine.render (p, block.getChannelPointer (0), block.getChannelPointer (1), 512);
            }

            float* L = buffer.getWritePointer (0);
            float* R = buffer.getWritePointer (1);

            for (int i = 0; i < 512; ++i)
            {
                float l = L[i], r = R[i];
                phaser.process (l, r, 6, 0.35f, 0.7f, 0.45f, 0.6f, 0.5f, 0.5f);
                ensemble.process (l, r, 0.6f, 0.6f, 0.5f);
                delay.process (l, r, 0.35f, 0.35f, 0.35f, 0.6f, false, 0.25f);
                out.process (l, r, 0.5f);
                L[i] = l; R[i] = r;
            }
        }

        const auto finish = std::chrono::high_resolution_clock::now();
        const double secs = std::chrono::duration<double> (finish - start).count();
        const double rendered = blocks * 512.0 / base;

        static const char* names[] = { "Eco (1x)", "High (2x)", "Ultra (4x)" };
        std::cout << "  " << names[quality]
                  << "  16 voices, 7x unison on both oscillators, all effects on: "
                  << juce::String (100.0 * secs / rendered, 1).toStdString()
                  << "% of one core" << std::endl;
    }
}

/** Renders a short note and returns the energy in its first `ms` milliseconds. */
double onsetEnergy (nd::EngineParams p, double sr, double ms)
{
    nd::SynthEngine engine;
    engine.prepare (sr, 4);
    engine.noteOn (60, 1.0f, p);

    const int n = (int) (sr * ms / 1000.0);
    std::vector<float> buf ((size_t) n, 0.0f);
    engine.render (p, buf.data(), buf.data(), n);

    double e = 0.0;
    for (float v : buf)
        e += (double) v * v;

    return e;
}

/** Parameters have to reach the DSP, not merely be stored in the tree.

    Every ADSR parameter and both LFO shapes were dead for the whole of this
    engine's first draft: the values were parsed, snapshotted into EngineParams, and
    then never handed to AdsrEnv or Lfo. Unit tests on the envelope passed, the
    preset renders passed, and the patches all still made a noise - just the wrong
    one. These checks assert the parameters change the audio. */
void testParametersReachTheDsp()
{
    section ("Parameters reach the DSP");

    const double sr = 96000.0;

    nd::EngineParams base;
    base.osc1.wave = nd::Wave::Saw;
    base.osc1.unison = 1;
    base.osc2.level = 0.0f;
    base.cutoff = 18000.0f;
    base.filterEnv = 0.0f;
    base.velToAmp = 0.0f;
    base.analogDrift = 0.0f;
    base.randomPhase = false;
    base.ampS = 1.0f;
    base.ampD = 4.0f;

    // --- amp attack ---
    nd::EngineParams fast = base; fast.ampA = 0.001f;
    nd::EngineParams slow = base; slow.ampA = 0.9f;

    const double eFast = onsetEnergy (fast, sr, 40.0);
    const double eSlow = onsetEnergy (slow, sr, 40.0);

    checkTrue (eFast > eSlow * 20.0,
               "amp attack time changes the onset",
               "fast/slow energy ratio " + juce::String (eFast / juce::jmax (eSlow, 1.0e-12), 1));

    // --- mod envelope, via the filter ---
    nd::EngineParams envFast = base; envFast.filterEnv = 0.9f; envFast.cutoff = 200.0f;
    envFast.modA = 0.001f; envFast.modD = 2.0f; envFast.modS = 1.0f;
    nd::EngineParams envSlow = envFast; envSlow.modA = 0.9f;

    const double mFast = onsetEnergy (envFast, sr, 40.0);
    const double mSlow = onsetEnergy (envSlow, sr, 40.0);

    checkTrue (mFast > mSlow * 2.0,
               "mod envelope attack changes the filter onset",
               "fast/slow energy ratio " + juce::String (mFast / juce::jmax (mSlow, 1.0e-12), 2));

    // --- release ---
    {
        nd::EngineParams shortRel = base; shortRel.ampR = 0.01f;
        nd::EngineParams longRel  = base; longRel.ampR = 2.0f;

        auto tailVoices = [&sr] (nd::EngineParams p)
        {
            nd::SynthEngine e;
            e.prepare (sr, 4);
            e.noteOn (60, 1.0f, p);
            std::vector<float> b ((size_t) 4096, 0.0f);
            for (int i = 0; i < 24; ++i) e.render (p, b.data(), b.data(), 4096);
            e.noteOff (60, p);
            for (int i = 0; i < 24; ++i) e.render (p, b.data(), b.data(), 4096);   // ~1 s
            return e.getActiveVoiceCount();
        };

        checkTrue (tailVoices (shortRel) == 0 && tailVoices (longRel) > 0,
                   "amp release time changes how long the voice sounds");
    }

    // --- LFO shape ---
    {
        auto renderShape = [&sr] (nd::LfoShape shape)
        {
            nd::EngineParams p;
            p.osc1.wave = nd::Wave::Saw; p.osc1.unison = 1; p.osc2.level = 0.0f;
            p.ampA = 0.001f; p.ampS = 1.0f; p.ampD = 8.0f;
            p.velToAmp = 0.0f; p.analogDrift = 0.0f; p.randomPhase = false;
            p.cutoff = 800.0f; p.resonance = 0.3f;
            p.lfo1Shape = shape;
            p.lfo1Rate = 6.0f;
            p.lfo1Retrig = true;
            p.mod[0] = { nd::ModSource::Lfo1, nd::ModDest::Cutoff, 0.9f };

            nd::SynthEngine e;
            e.prepare (sr, 4);
            e.noteOn (48, 1.0f, p);

            const int n = (int) (sr * 0.4);
            std::vector<float> b ((size_t) n, 0.0f);
            e.render (p, b.data(), b.data(), n);
            return b;
        };

        const auto sine = renderShape (nd::LfoShape::Sine);
        const auto square = renderShape (nd::LfoShape::Square);

        double diff = 0.0;
        for (size_t i = 0; i < sine.size(); ++i)
            diff += std::abs ((double) sine[i] - (double) square[i]);
        diff /= (double) sine.size();

        checkTrue (diff > 1.0e-3,
                   "LFO shape changes the modulation",
                   "mean |sine - square| = " + juce::String (diff, 6));
    }
}

/** Minimal host so the preset bank can be checked against the real parameter layout
    without pulling in the plugin wrapper. */
class TestHost : public juce::AudioProcessor
{
public:
    TestHost() : apvts (*this, nullptr, "NITEDRIVE", ndp::createLayout()) {}

    const juce::String getName() const override { return "test"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& d) override { ndp::writeState (apvts, program, d); }
    void setStateInformation (const void* d, int n) override { ndp::readState (apvts, d, n, program); }

    int program = 0;

    juce::AudioProcessorValueTreeState apvts;
};

void testPresets()
{
    section ("Presets and parameters");

    TestHost host;

    checkTrue (host.getParameters().size() > 80,
               "parameter layout is complete",
               juce::String (host.getParameters().size()) + " parameters");

    // Every preset must name real parameters and stay inside their ranges, otherwise
    // loading it silently clamps and the patch does not sound as authored.
    int unresolvedIds = 0;
    bool allInRange = true;
    juce::String badRange;

    for (int i = 0; i < ndp::getNumPresets(); ++i)
    {
        unresolvedIds += ndp::applyPreset (host.apvts, i);

        for (auto* param : host.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                const float v = ranged->convertFrom0to1 (ranged->getValue());
                if (! std::isfinite (v))
                {
                    allInRange = false;
                    badRange = ranged->getParameterID();
                }
            }
        }

        checkTrue (ndp::getPresetName (i).isNotEmpty(),
                   "preset " + juce::String (i) + " has a name: " + ndp::getPresetName (i));
    }

    checkTrue (unresolvedIds == 0, "every preset id exists in the layout",
               juce::String (unresolvedIds) + " unresolved");
    checkTrue (allInRange, "every preset value is finite after loading", badRange);

    // State round-trip: every parameter must come back with the value it went out
    // with. Compared per parameter rather than by tree equality so a failure names
    // the offender instead of just saying "something changed".
    ndp::applyPreset (host.apvts, 3);

    std::map<juce::String, float> before;
    for (auto* p : host.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            before[r->getParameterID()] = r->getValue();

    const auto xml = host.apvts.copyState().createXml();
    ndp::applyPreset (host.apvts, 0);
    host.apvts.replaceState (juce::ValueTree::fromXml (*xml));

    float worstDelta = 0.0f;
    juce::String worstId;

    for (auto* p : host.getParameters())
    {
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const float delta = std::abs (r->getValue() - before[r->getParameterID()]);
            if (delta > worstDelta)
            {
                worstDelta = delta;
                worstId = r->getParameterID();
            }
        }
    }

    checkTrue (worstDelta < 1.0e-5f,
               "parameter state survives a save/load round trip",
               worstDelta > 0.0f ? worstId + " drifted by " + juce::String (worstDelta, 8)
                                 : juce::String ("exact"));

    // The same round trip through the binary blob the host actually stores, this time
    // including the program index.
    ndp::applyPreset (host.apvts, 5);
    host.program = 5;

    std::map<juce::String, float> blobBefore;
    for (auto* p : host.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            blobBefore[r->getParameterID()] = r->getValue();

    juce::MemoryBlock blob;
    host.getStateInformation (blob);

    ndp::applyPreset (host.apvts, 0);
    host.program = 0;
    host.setStateInformation (blob.getData(), (int) blob.getSize());

    checkTrue (host.program == 5, "program index survives the host state blob",
               "got " + juce::String (host.program));

    float blobDelta = 0.0f;
    for (auto* p : host.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            blobDelta = juce::jmax (blobDelta, std::abs (r->getValue() - blobBefore[r->getParameterID()]));

    checkTrue (blobDelta < 1.0e-5f, "host state blob restores every parameter",
               "worst drift " + juce::String (blobDelta, 8));

    // Garbage must be rejected rather than half-applied.
    const char junk[] = "not a nitedrive state at all";
    const float sentinel = host.getParameters()[0]->getValue();
    host.setStateInformation (junk, (int) sizeof (junk));

    checkTrue (host.getParameters()[0]->getValue() == sentinel,
               "foreign state data is rejected without corrupting the patch");
}

/** Renders every preset through the engine to prove none of them is silent, blown
    out, or full of NaN. Preset values are pushed through the same parameter objects
    the plugin uses, then read back into an EngineParams. */
void testPresetAudio()
{
    section ("Preset audio");

    TestHost host;
    const double sr = 96000.0;

    for (int i = 0; i < ndp::getNumPresets(); ++i)
    {
        ndp::applyPreset (host.apvts, i);

        auto value = [&host] (const char* id) -> float
        {
            auto* p = host.apvts.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        nd::EngineParams p;
        p.osc1.wave    = (nd::Wave) (int) value ("osc1Wave");
        p.osc1.level   = value ("osc1Level");
        p.osc1.octave  = (int) std::lround (value ("osc1Octave"));
        p.osc1.coarse  = value ("osc1Coarse");
        p.osc1.fine    = value ("osc1Fine");
        p.osc1.pulseWidth = value ("osc1PW");
        p.osc1.detune  = value ("osc1Detune");
        p.osc1.spread  = value ("osc1Spread");
        p.osc1.unison  = (int) std::lround (value ("osc1Unison"));
        p.osc2.wave    = (nd::Wave) (int) value ("osc2Wave");
        p.osc2.level   = value ("osc2Level");
        p.osc2.octave  = (int) std::lround (value ("osc2Octave"));
        p.osc2.coarse  = value ("osc2Coarse");
        p.osc2.unison  = (int) std::lround (value ("osc2Unison"));
        p.osc2.detune  = value ("osc2Detune");
        p.osc2Sync     = value ("osc2Sync") > 0.5f;
        p.subLevel     = value ("subLevel");
        p.subOctave    = (int) std::lround (value ("subOctave"));
        p.noiseLevel   = value ("noiseLevel");
        p.ringLevel    = value ("ringLevel");
        p.filterMode   = (nd::FilterMode) (int) value ("filterMode");
        p.cutoff       = value ("cutoff");
        p.resonance    = value ("resonance");
        p.filterDrive  = value ("filterDrive");
        p.keyTrack     = value ("keyTrack");
        p.filterEnv    = value ("filterEnv");
        p.ampA = value ("ampA"); p.ampD = value ("ampD");
        p.ampS = value ("ampS"); p.ampR = value ("ampR");
        p.modA = value ("modA"); p.modD = value ("modD");
        p.modS = value ("modS"); p.modR = value ("modR");
        p.preDriveType  = (nd::DriveType) (int) value ("preDriveType");
        p.preDrive      = value ("preDrive");
        p.postDriveType = (nd::DriveType) (int) value ("postDriveType");
        p.postDrive     = value ("postDrive");
        p.voiceMode     = (nd::VoiceMode) (int) value ("voiceMode");
        p.glideTime     = value ("glide");

        for (int m = 0; m < nd::kNumModSlots; ++m)
        {
            const auto n = juce::String (m + 1);
            p.mod[m].source = (nd::ModSource) (int) value (("modSrc" + n).toRawUTF8());
            p.mod[m].dest   = (nd::ModDest)   (int) value (("modDst" + n).toRawUTF8());
            p.mod[m].amount = value (("modAmt" + n).toRawUTF8());
        }

        nd::SynthEngine engine;
        engine.prepare (sr, 8);

        const int n = (int) (sr * 1.5);
        std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);

        engine.noteOn (48, 0.95f, p);
        engine.noteOn (55, 0.95f, p);

        int pos = 0;
        while (pos < n)
        {
            const int blockSize = juce::jmin (256, n - pos);
            if (pos >= (int) (sr * 0.8) && pos < (int) (sr * 0.8) + 256)
            {
                engine.noteOff (48, p);
                engine.noteOff (55, p);
            }
            engine.render (p, &L[(size_t) pos], &R[(size_t) pos], blockSize);
            pos += blockSize;
        }

        double rms = 0.0;
        float peak = 0.0f;
        for (int s = 0; s < n; ++s)
        {
            rms += (double) L[(size_t) s] * L[(size_t) s];
            peak = juce::jmax (peak, std::abs (L[(size_t) s]));
        }
        rms = std::sqrt (rms / n);

        const auto name = ndp::getPresetName (i);
        checkTrue (allFinite (L.data(), n) && allFinite (R.data(), n),
                   "\"" + name + "\" renders without NaN");
        checkTrue (rms > 0.002 && peak < 30.0f,
                   "\"" + name + "\" has a usable level",
                   "rms " + juce::String (rms, 4) + ", peak " + juce::String (peak, 2));
    }
}

/** Renders every factory preset through the full signal path - engine plus the
    effects the preset enables - and prints measured acoustic features as JSON.

    Not a pass/fail test: this is the evidence base for judging whether each patch
    matches the sound it is named after. Run with NITEDRIVE_DUMP_FEATURES=1. */
namespace features
{

struct NotePlan
{
    const char* name;
    std::vector<int> notes;
    double gateSeconds;
    double totalSeconds;
};

const NotePlan kPlans[] = {
    { "Init",          { 60 },             1.2, 3.0 },
    { "Nite Bass",     { 33 },             1.2, 3.0 },
    { "Rave Stab",     { 45, 52, 57 },     0.2, 3.0 },
    { "Acid Drive",    { 45 },             1.2, 3.0 },
    { "Supersaw Lead", { 69 },             1.5, 3.5 },
    { "Fuzz Chords",   { 50, 57, 62 },     1.0, 3.0 },
    { "Sync Scream",   { 57 },             1.2, 3.0 },
    { "Ring Metal",    { 60 },             1.0, 3.0 },
    { "Juno Pad",      { 48, 55, 60, 64 }, 2.0, 4.5 },
    { "Pump Saws",     { 45, 52, 57 },     1.5, 3.5 },
    { "Sub Thump",     { 33 },             0.25, 2.0 },
    { "Noise Sweep",   { 48 },             2.0, 4.0 },
    { "Electro Clap",  { 60 },             0.1, 2.0 },
    { "Compute Bleep", { 69 },             0.15, 2.5 }
};

void dump (std::ostream& json)
{
    TestHost host;

    // kPlans is a hand-maintained parallel of the preset bank; a mismatch would
    // read past the array or silently attribute measurements to the wrong patch.
    const int numPlans = (int) (sizeof (kPlans) / sizeof (kPlans[0]));
    if (numPlans != ndp::getNumPresets())
    {
        json << "[]" << std::endl;
        std::cerr << "features::kPlans has " << numPlans << " entries but the bank has "
                  << ndp::getNumPresets() << " presets - update kPlans" << std::endl;
        jassertfalse;
        return;
    }

    for (int i = 0; i < ndp::getNumPresets(); ++i)
    {
        if (ndp::getPresetName (i) != kPlans[i].name)
        {
            json << "[]" << std::endl;
            std::cerr << "features::kPlans[" << i << "] is '" << kPlans[i].name
                      << "' but the bank has '" << ndp::getPresetName (i).toStdString()
                      << "' - order mismatch" << std::endl;
            jassertfalse;
            return;
        }
    }
    // The shipping default is High quality: voices at 2x the host rate. Measuring at
    // the voice rate (FX are rate-aware) keeps the nonlinear clamps and filter
    // behaviour the same as what users hear.
    const double sr = 96000.0;

    json << "[" << std::endl;

    for (int i = 0; i < ndp::getNumPresets(); ++i)
    {
        ndp::applyPreset (host.apvts, i);

        auto value = [&host] (const char* id) -> float
        {
            auto* p = host.apvts.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        nd::EngineParams p;
        p.osc1.wave    = (nd::Wave) (int) value ("osc1Wave");
        p.osc1.level   = value ("osc1Level");
        p.osc1.octave  = (int) std::lround (value ("osc1Octave"));
        p.osc1.coarse  = value ("osc1Coarse");
        p.osc1.fine    = value ("osc1Fine");
        p.osc1.pulseWidth = value ("osc1PW");
        p.osc1.detune  = value ("osc1Detune");
        p.osc1.spread  = value ("osc1Spread");
        p.osc1.unison  = (int) std::lround (value ("osc1Unison"));
        p.osc2.wave    = (nd::Wave) (int) value ("osc2Wave");
        p.osc2.level   = value ("osc2Level");
        p.osc2.octave  = (int) std::lround (value ("osc2Octave"));
        p.osc2.coarse  = value ("osc2Coarse");
        p.osc2.fine    = value ("osc2Fine");
        p.osc2.pulseWidth = value ("osc2PW");
        p.osc2.detune  = value ("osc2Detune");
        p.osc2.spread  = value ("osc2Spread");
        p.osc2.unison  = (int) std::lround (value ("osc2Unison"));
        p.osc2Sync     = value ("osc2Sync") > 0.5f;
        p.subWave      = (nd::SubWave) (int) value ("subWave");
        p.subOctave    = (int) std::lround (value ("subOctave"));
        p.subLevel     = value ("subLevel");
        p.noiseLevel   = value ("noiseLevel");
        p.ringLevel    = value ("ringLevel");
        p.filterMode   = (nd::FilterMode) (int) value ("filterMode");
        p.cutoff       = value ("cutoff");
        p.resonance    = value ("resonance");
        p.filterDrive  = value ("filterDrive");
        p.keyTrack     = value ("keyTrack");
        p.filterEnv    = value ("filterEnv");
        p.velToCutoff  = value ("velToCutoff");
        p.ampA = value ("ampA"); p.ampD = value ("ampD");
        p.ampS = value ("ampS"); p.ampR = value ("ampR");
        p.velToAmp = value ("velToAmp");
        p.modA = value ("modA"); p.modD = value ("modD");
        p.modS = value ("modS"); p.modR = value ("modR");
        p.lfo1Shape = (nd::LfoShape) (int) value ("lfo1Shape");
        p.lfo2Shape = (nd::LfoShape) (int) value ("lfo2Shape");
        p.lfo1Rate = value ("lfo1Rate");
        p.lfo2Rate = value ("lfo2Rate");
        p.lfo1Retrig = value ("lfo1Retrig") > 0.5f;
        p.lfo2Retrig = value ("lfo2Retrig") > 0.5f;
        p.preDriveType  = (nd::DriveType) (int) value ("preDriveType");
        p.preDrive      = value ("preDrive");
        p.postDriveType = (nd::DriveType) (int) value ("postDriveType");
        p.postDrive     = value ("postDrive");
        p.voiceMode     = (nd::VoiceMode) (int) value ("voiceMode");
        p.glideTime     = value ("glide");
        p.randomPhase   = value ("randomPhase") > 0.5f;
        p.analogDrift   = value ("drift");

        for (int m = 0; m < nd::kNumModSlots; ++m)
        {
            const auto n = juce::String (m + 1);
            p.mod[m].source = (nd::ModSource) (int) value (("modSrc" + n).toRawUTF8());
            p.mod[m].dest   = (nd::ModDest)   (int) value (("modDst" + n).toRawUTF8());
            p.mod[m].amount = value (("modAmt" + n).toRawUTF8());
        }

        const auto& plan = kPlans[i];

        nd::SynthEngine engine;
        engine.prepare (sr, 12);

        nd::Phaser phaser; phaser.prepare (sr);
        nd::Ensemble ensemble; ensemble.prepare (sr);
        nd::StereoDelay delay; delay.prepare (sr);
        nd::OutputStage out; out.prepare (sr);

        const bool phOn = value ("phaserOn") > 0.5f;
        const bool enOn = value ("ensembleOn") > 0.5f;
        const bool dlOn = value ("delayOn") > 0.5f;
        const bool pmOn = value ("pumpOn") > 0.5f;

        const double bpm = 120.0;
        float dTime = value ("delayTime");
        if (value ("delaySync") > 0.5f)
        {
            const int di = juce::jlimit (0, ndp::kNumDivisions - 1,
                                         (int) std::lround (value ("delayDivision")));
            dTime = (float) ((double) ndp::kDivisions[di].beats * 60.0 / bpm);
        }
        const int pi = juce::jlimit (0, ndp::kNumDivisions - 1,
                                     (int) std::lround (value ("pumpDivision")));
        const double pumpInc = (bpm / 60.0) / juce::jmax (0.01f, ndp::kDivisions[pi].beats) / sr;

        const float outGain = nd::dbToGain (value ("outputGain"));

        const int total = (int) (sr * plan.totalSeconds);
        const int gate  = (int) (sr * plan.gateSeconds);

        std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
        std::vector<float> dryL ((size_t) total, 0.0f);

        for (int note : plan.notes)
            engine.noteOn (note, 0.9f, p);

        int pos = 0;
        double pumpPhase = 0.0;

        while (pos < total)
        {
            const int block = juce::jmin (256, total - pos);

            if (pos < gate && pos + block >= gate)
                for (int note : plan.notes)
                    engine.noteOff (note, p);

            engine.render (p, &L[(size_t) pos], &R[(size_t) pos], block);

            // Attack/release are properties of the patch, not of its delay tail or
            // pump duck, so the envelope metrics read the dry engine output.
            for (int s2 = 0; s2 < block; ++s2)
                dryL[(size_t) (pos + s2)] = L[(size_t) (pos + s2)];

            for (int s2 = 0; s2 < block; ++s2)
            {
                float& l = L[(size_t) (pos + s2)];
                float& r = R[(size_t) (pos + s2)];

                if (phOn) phaser.process (l, r,
                        juce::jlimit (2, 12, (int) std::lround (value ("phaserStages"))),
                        value ("phaserRate"), value ("phaserDepth"), value ("phaserCentre"),
                        value ("phaserFeedback"), value ("phaserSpread"), value ("phaserMix"));
                if (enOn) ensemble.process (l, r, value ("ensembleRate"),
                        value ("ensembleDepth"), value ("ensembleMix"));
                if (dlOn) delay.process (l, r, dTime, dTime * (1.0f + value ("delayOffset")),
                        value ("delayFeedback"), value ("delayTone"),
                        value ("delayPingPong") > 0.5f, value ("delayMix"));
                if (pmOn)
                {
                    const float g = nd::Pump::gainFor ((float) pumpPhase,
                            value ("pumpDepth"), value ("pumpShape"));
                    l *= g; r *= g;
                }
                pumpPhase += pumpInc;
                if (pumpPhase >= 1.0) pumpPhase -= 1.0;

                out.process (l, r, outGain);
            }

            pos += block;
        }

        // ---- measurements ----
        const int envWin = (int) (sr * 0.002);
        std::vector<double> env;
        for (int w = 0; w + envWin <= total; w += envWin)
        {
            double acc = 0.0;
            for (int s2 = 0; s2 < envWin; ++s2)
                acc += (double) dryL[(size_t) (w + s2)] * dryL[(size_t) (w + s2)];
            env.push_back (std::sqrt (acc / envWin));
        }

        double envPeak = 0.0; size_t envPeakAt = 0;
        for (size_t e = 0; e < env.size(); ++e)
            if (env[e] > envPeak) { envPeak = env[e]; envPeakAt = e; }

        double attackMs = -1.0;
        for (size_t e = 0; e < env.size(); ++e)
            if (env[e] >= 0.9 * envPeak) { attackMs = e * 2.0; break; }

        // Release measured from the level AT note-off; a patch that already decayed
        // to silence reports null (-1) instead of a misleading 0.
        const size_t gateWin = juce::jmin (env.size() - 1, (size_t) (gate / envWin));
        double releaseMs = -1.0;
        const double offLevel = env[gateWin];
        if (offLevel > envPeak * 0.02)
            for (size_t e = gateWin; e < env.size(); ++e)
                if (env[e] < offLevel * 0.01) { releaseMs = (e - gateWin) * 2.0; break; }

        float peak = 0.0f;
        double rms = 0.0;
        int rmsN = 0;
        double corrLR = 0.0, pL = 0.0, pR = 0.0;
        for (int s2 = 0; s2 < total; ++s2)
        {
            peak = juce::jmax (peak, std::abs (L[(size_t) s2]), std::abs (R[(size_t) s2]));
            if (s2 < gate) { rms += (double) L[(size_t) s2] * L[(size_t) s2]; ++rmsN; }
            corrLR += (double) L[(size_t) s2] * R[(size_t) s2];
            pL += (double) L[(size_t) s2] * L[(size_t) s2];
            pR += (double) R[(size_t) s2] * R[(size_t) s2];
        }
        rms = std::sqrt (rms / juce::jmax (1, rmsN));
        const double corr = corrLR / juce::jmax (1e-12, std::sqrt (pL * pR));

        // Spectrum window sized to the sound: a stab is measured over its body plus a
        // little tail (zero-padded), a sustained patch over the settled middle of its
        // gate - not a fixed window that mostly covers silence.
        int fftStart, fftLen;
        if (plan.gateSeconds < 0.5)
        {
            fftStart = 0;
            fftLen = juce::jmin (kFftSize, (int) (sr * (plan.gateSeconds + 0.3)));
        }
        else
        {
            fftStart = (int) (sr * juce::jmin (0.4, plan.gateSeconds * 0.25));
            fftLen = juce::jmin (kFftSize, gate - fftStart);
        }

        juce::dsp::FFT fft (kFftOrder);
        std::vector<float> spec ((size_t) kFftSize * 2, 0.0f);
        const auto win = blackmanHarris (fftLen);
        for (int s2 = 0; s2 < fftLen && fftStart + s2 < total; ++s2)
            spec[(size_t) s2] = L[(size_t) (fftStart + s2)] * win[(size_t) s2];
        fft.performFrequencyOnlyForwardTransform (spec.data());

        // Infrasound is reported as its own band rather than silently absorbed into
        // "sub" - un-guarded, it manufactured a false review finding. Analysis stops
        // at 20 kHz so the 96 kHz render's ultrasonic content can't skew ratios.
        const double binHz = sr / (double) kFftSize;
        double centroidNum = 0.0, centroidDen = 0.0;
        double eInf = 0.0, eSub = 0.0, eBass = 0.0, eMid = 0.0, eHigh = 0.0, eAir = 0.0;
        for (int b = 1; b < kFftSize / 2; ++b)
        {
            const double f = b * binHz;
            if (f > 20000.0)
                break;

            const double pw = (double) spec[(size_t) b] * spec[(size_t) b];
            if (f >= 20.0) { centroidNum += f * pw; centroidDen += pw; }
            if      (f < 20.0)   eInf  += pw;
            else if (f < 120.0)  eSub  += pw;
            else if (f < 350.0)  eBass += pw;
            else if (f < 2000.0) eMid  += pw;
            else if (f < 6000.0) eHigh += pw;
            else                 eAir  += pw;
        }
        const double eTot = juce::jmax (1e-30, eInf + eSub + eBass + eMid + eHigh + eAir);

        auto db = [] (double v) { return 20.0 * std::log10 (juce::jmax (1e-12, v)); };

        // Preset names are compile-time literals today, but an unescaped quote or
        // backslash would corrupt the JSON the gain-staging test parses.
        json << "  {\"preset\": \"" << juce::String (plan.name).replace ("\\", "\\\\")
                                                                 .replace ("\"", "\\\"") << "\""
                  << ", \"peak_db\": "   << juce::String (db (peak), 1)
                  << ", \"rms_db\": "    << juce::String (db (rms), 1)
                  << ", \"crest_db\": "  << juce::String (db (peak) - db (rms), 1)
                  << ", \"attack_ms\": " << juce::String (attackMs, 0)
                  << ", \"release_ms\": " << juce::String (releaseMs, 0)
                  << ", \"centroid_hz\": " << juce::String (centroidDen > 0 ? centroidNum / centroidDen : 0.0, 0)
                  << ", \"infra_pct\": " << juce::String (100.0 * eInf / eTot, 1)
                  << ", \"sub_pct\": "   << juce::String (100.0 * eSub / eTot, 1)
                  << ", \"bass_pct\": "  << juce::String (100.0 * eBass / eTot, 1)
                  << ", \"mid_pct\": "   << juce::String (100.0 * eMid / eTot, 1)
                  << ", \"high_pct\": "  << juce::String (100.0 * eHigh / eTot, 1)
                  << ", \"air_pct\": "   << juce::String (100.0 * eAir / eTot, 1)
                  << ", \"stereo_corr\": " << juce::String (corr, 2)
                  << "}" << (i + 1 < ndp::getNumPresets() ? "," : "") << std::endl;
    }

    json << "]" << std::endl;
}

inline void dump() { dump (std::cout); }

} // namespace features

/** The preset bank is a product surface: browsing it at fixed monitor level must
    not jump between too-hot and inaudible. Renders every preset through the full
    chain (the features::dump path) and holds the bank to a peak window. The review
    that motivated this measured a 12.4 dB peak spread with the pad louder than the
    lead. */
void testBankGainStaging()
{
    section ("Preset bank gain staging");

    std::ostringstream oss;
    features::dump (oss);

    const auto parsed = juce::JSON::parse (juce::String (oss.str()));
    const auto* arr = parsed.getArray();

    checkTrue (arr != nullptr && arr->size() == ndp::getNumPresets(),
               "feature dump renders every preset",
               arr ? juce::String (arr->size()) : "parse failed");

    if (arr == nullptr)
        return;

    double lo = 0.0, hi = -200.0;
    juce::String loName, hiName;
    bool infraOk = true;
    juce::String infraName;

    for (const auto& v : *arr)
    {
        const double peak = (double) v.getProperty ("peak_db", -200.0);
        const auto name = v.getProperty ("preset", "?").toString();

        if (peak < lo || lo == 0.0) { lo = peak; loName = name; }
        if (peak > hi) { hi = peak; hiName = name; }

        // Sustained inaudible output eats headroom and endangers subwoofers. The
        // measured worst offender before the fix was 50% of ALL rendered energy.
        const double infra = (double) v.getProperty ("infra_pct", 0.0);
        if (infra > 15.0) { infraOk = false; infraName = name + " " + juce::String (infra, 1) + "%"; }
    }

    checkTrue (hi <= -2.0 && lo >= -12.0,
               "every preset peaks inside the -12..-2 dBFS window",
               "range " + juce::String (lo, 1) + " (" + loName + ") .. "
                   + juce::String (hi, 1) + " (" + hiName + ")");
    checkBelow (hi - lo, 4.5, "bank peak spread", "dB");
    checkTrue (infraOk, "no preset spends >15% of its energy below 20 Hz", infraName);
}


} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (std::getenv ("NITEDRIVE_DUMP_FEATURES") != nullptr)
    {
        features::dump();
        return 0;
    }

    std::cout << "NITEDRIVE DSP verification" << std::endl;

    testWaveTables();
    testOscillatorAliasing();
    testFilter();
    testShaperAntiderivatives();
    testStereoSymmetry();
    testEnvelope();
    testEngine();
    testGlide();
    testParametersReachTheDsp();
    testPresets();
    testPresetAudio();
    testBankGainStaging();
    reportPerformance();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed" << std::endl;

    if (failures > 0)
        std::cout << failures << " FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
