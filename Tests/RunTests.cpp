/*
    Offline DSP verification for NITEDRIVE.

    These are measurements, not smoke tests: each one renders audio and puts a number
    on it. The thresholds are set just loose enough to absorb platform float
    differences and tight enough that a real regression trips them.
*/

#include <JuceHeader.h>

#include <chrono>

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

constexpr int kFftOrder = 15;
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
double aliasRatioDb (const float* x, int n, double f0, double sr, double fmax, int tolBins = 6)
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

    for (int wave = 0; wave < 4; ++wave)
    {
        double worst = -200.0;
        double worstFreq = 0.0;

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
        }

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
            checkWithin (f, 440.0, 40.0,
                         "legato-only glide starts a fresh note in tune", " Hz");
        else
            checkTrue (f < 260.0,
                       "always-glide slides the fresh note up from the previous one",
                       juce::String (f, 1) + " Hz at onset, target 440 Hz");
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
    bool allIdsValid = true;
    bool allInRange = true;
    juce::String badId, badRange;

    for (int i = 0; i < ndp::getNumPresets(); ++i)
    {
        ndp::applyPreset (host.apvts, i);

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

    checkTrue (allIdsValid, "every preset id exists in the layout", badId);
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

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "NITEDRIVE DSP verification" << std::endl;

    testWaveTables();
    testOscillatorAliasing();
    testFilter();
    testShaperAntiderivatives();
    testEnvelope();
    testEngine();
    testGlide();
    testPresets();
    testPresetAudio();
    reportPerformance();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed" << std::endl;

    if (failures > 0)
        std::cout << failures << " FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
