#pragma once

#include "DelayLine.h"
#include "Filter.h"
#include "Lfo.h"
#include "Saturation.h"

namespace nd
{

/** First-order allpass in TPT form, the phaser's building block. */
class Allpass1
{
public:
    void reset() noexcept { z = 0.0f; }

    float process (float x, float G) noexcept
    {
        const float v  = (x - z) * G;
        const float lp = v + z;
        z = fd (lp + v);
        return 2.0f * lp - x;      // allpass == 2*lowpass - input
    }

private:
    float z = 0.0f;
};

/** Swept phaser.

    Up to twelve allpass stages with a resonant feedback path. This is the effect
    that puts the swirl on a Nite Versions lead, and it wants to sit *after* the
    distortion so it phases the harmonics the drive created rather than the ones it
    is about to. */
class Phaser
{
public:
    static constexpr int kMaxStages = 12;

    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        lfo.prepare (sampleRate, 0x51ED270Bu);
        lfo.setShape (LfoShape::Sine);
        reset();
    }

    void reset() noexcept
    {
        for (auto& a : apL) a.reset();
        for (auto& a : apR) a.reset();
        fbL = fbR = 0.0f;
        activeStages = kMaxStages;
        lfo.reset (0.0f);
    }

    /** @param stages    2..12, in pairs
        @param rateHz    sweep rate
        @param depth     0..1 sweep range
        @param centre    0..1 sweep centre, mapped 100 Hz .. 8 kHz
        @param feedback  -0.95..0.95, negative gives the hollower tone
        @param spread    0..1 stereo offset between the two channels
        @param mix       0..1 */
    void process (float& l, float& r, int stages, float rateHz, float depth,
                  float centre, float feedback, float spread, float mix) noexcept
    {
        const float m = lfo.process (rateHz);

        const float base = 100.0f * std::exp2 (clampf (centre, 0.0f, 1.0f) * 6.3f);
        const float sweep = clampf (depth, 0.0f, 1.0f) * 2.2f;

        // Spread 0 sweeps both channels together; spread 1 runs them in opposition.
        const float mR = lerp (m, -m, clampf (spread, 0.0f, 1.0f));
        const float fL = clampf (base * fastExp2 (m  * sweep), 20.0f, 0.45f * sr);
        const float fR = clampf (base * fastExp2 (mR * sweep), 20.0f, 0.45f * sr);

        const int n = (int) clampf ((float) stages, 2.0f, (float) kMaxStages);
        const float fb = clampf (feedback, -0.95f, 0.95f);

        // Stages above the current count keep their last sample indefinitely, so
        // raising Stages - which happens on any preset change - would push that
        // frozen audio back through the feedback loop as a burst out of silence.
        if (n > activeStages)
            for (int i = activeStages; i < n; ++i)
            {
                apL[i].reset();
                apR[i].reset();
            }

        activeStages = n;

        float xl = l + fbL * fb;
        float xr = r + fbR * fb;

        const float gl = coeff (fL);
        const float gr = coeff (fR);

        for (int i = 0; i < n; ++i) xl = apL[i].process (xl, gl);
        for (int i = 0; i < n; ++i) xr = apR[i].process (xr, gr);

        fbL = fd (xl);
        fbR = fd (xr);

        const float w = clampf (mix, 0.0f, 1.0f);
        l = lerp (l, xl, w);
        r = lerp (r, xr, w);
    }

private:
    float coeff (float fc) const noexcept
    {
        const float x  = kPi * fc / sr;
        const float x2 = x * x;
        const float g  = x * (1.0f + x2 * (0.3333333f + x2 * (0.1333333f + x2 * 0.0539683f)));
        return g / (1.0f + g);
    }

    float sr = 48000.0f;
    Allpass1 apL[kMaxStages], apR[kMaxStages];
    float fbL = 0.0f, fbR = 0.0f;
    int activeStages = kMaxStages;
    Lfo lfo;
};

/** Three-voice ensemble chorus.

    Modelled on the Juno's stereo ensemble rather than a modern studio chorus: short
    delays, a fairly deep sweep, and the two outer voices running in anti-phase so
    the image opens up instead of just smearing. */
class Ensemble
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        line.prepare (sampleRate, 0.06f);
        for (int i = 0; i < 3; ++i)
        {
            lfos[i].prepare (sampleRate, 0x1234567u + (uint32_t) i * 7919u);
            lfos[i].setShape (LfoShape::Sine);
            lfos[i].reset ((float) i / 3.0f);
        }
    }

    void reset() noexcept
    {
        line.reset();
        for (int i = 0; i < 3; ++i) lfos[i].reset ((float) i / 3.0f);
    }

    /** @param rateHz  0.05..8
        @param depth   0..1
        @param mix     0..1 */
    void process (float& l, float& r, float rateHz, float depth, float mix) noexcept
    {
        // One line, three modulated taps: the three voices read the same delayed
        // signal at different offsets, so three separate buffers were pure waste.
        const float mono = 0.5f * (l + r);
        line.write (mono);

        const float d = clampf (depth, 0.0f, 1.0f);
        const float baseMs = 8.0f;
        const float swingMs = 5.5f * d;

        float wetL = 0.0f, wetR = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float m = lfos[i].process (rateHz);
            const float ms = baseMs + swingMs * m + (float) i * 2.5f;
            const float v = line.read (ms * 0.001f * sr);

            // Outer voices in anti-phase across the stereo field.
            if (i == 0) { wetL += v;        wetR += v * 0.35f; }
            if (i == 1) { wetL += v * 0.7f; wetR += v * 0.7f; }
            if (i == 2) { wetL += v * 0.35f; wetR += v; }
        }

        const float w = clampf (mix, 0.0f, 1.0f) * 0.5f;
        l = l * (1.0f - w * 0.5f) + wetL * w;
        r = r * (1.0f - w * 0.5f) + wetR * w;
    }

private:
    float sr = 48000.0f;
    DelayLine line;
    Lfo lfos[3];
};

/** Stereo delay with a filtered feedback path and an optional ping-pong cross. */
class StereoDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        left.prepare (sampleRate, 4.0f);
        right.prepare (sampleRate, 4.0f);
        lpL.prepare (sampleRate); lpR.prepare (sampleRate);
        hpL.prepare (sampleRate); hpR.prepare (sampleRate);
        smoothL.setTimeConstant (0.05f, sampleRate);
        smoothR.setTimeConstant (0.05f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        left.reset(); right.reset();
        lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset();
        smoothL.reset (0.0f); smoothR.reset (0.0f);
        primed = false;
    }

    /** @param timeL/timeR  delay in seconds
        @param feedback     0..0.98
        @param tone         0..1, darkens the repeats
        @param pingPong     cross the feedback between channels
        @param mix          0..1 */
    void process (float& l, float& r, float timeL, float timeR, float feedback,
                  float tone, bool pingPong, float mix) noexcept
    {
        const float targetL = clampf (timeL, 0.001f, 3.9f) * sr;
        const float targetR = clampf (timeR, 0.001f, 3.9f) * sr;

        // Snap on the first sample. Smoothing up from zero would sweep the first
        // repeat in from a near-zero delay, which is audible every time the delay is
        // switched on.
        if (! primed)
        {
            smoothL.reset (targetL);
            smoothR.reset (targetR);
            primed = true;
        }

        const float dl = smoothL.process (targetL);
        const float dr = smoothR.process (targetR);

        const float outL = left.read (dl);
        const float outR = right.read (dr);

        // Repeats get progressively darker and thinner, the way tape echo does.
        const float cutoff = 400.0f * std::exp2 (clampf (tone, 0.0f, 1.0f) * 5.6f);
        float rl = lpL.processLP (outL, cutoff);
        float rr = lpR.processLP (outR, cutoff);
        rl = hpL.process (rl, 120.0f);
        rr = hpR.process (rr, 120.0f);

        const float fb = clampf (feedback, 0.0f, 0.98f);

        if (pingPong)
        {
            left.write (l + rr * fb);
            right.write (r + rl * fb);
        }
        else
        {
            left.write (l + rl * fb);
            right.write (r + rr * fb);
        }

        const float w = clampf (mix, 0.0f, 1.0f);
        l += outL * w;
        r += outR * w;
    }

private:
    /** One-pole lowpass sharing the high-pass helper's shape. */
    struct LP
    {
        void prepare (double sampleRate) noexcept { sr = (float) sampleRate; reset(); }
        void reset() noexcept { z = 0.0f; }
        float processLP (float x, float fc) noexcept
        {
            // The linear form (2*pi*fc/sr) saturates at exactly 1.0 - the filter
            // becomes a wire - once fc passes sr/2pi, which is only 7.6 kHz at
            // 48 kHz: the top quarter of the Tone knob was doing nothing, and the
            // realised corner moved with the sample rate.
            const float g = 1.0f - std::exp (-kTwoPi * clampf (fc, 0.0f, 0.49f * sr) / sr);
            z = fd (z + g * (x - z));
            return z;
        }
        float sr = 48000.0f, z = 0.0f;
    };

    float sr = 48000.0f;
    DelayLine left, right;
    LP lpL, lpR;
    OnePoleHP hpL, hpR;
    OnePole smoothL, smoothR;
    bool primed = false;
};

/** Tempo-locked ducker.

    Not a compressor - it is the sidechain-pump effect, generated from the host's
    transport rather than from a detector, so it stays locked whatever the patch is
    doing and works on sustained pads that would never trigger a real sidechain. */
struct Pump
{
    /** @param beatPhase  position within the pump period, 0..1
        @param depth      0..1
        @param shape      0..1, from a slow swell to a sharp duck */
    static float gainFor (float beatPhase, float depth, float shape) noexcept
    {
        const float p = beatPhase - std::floor (beatPhase);
        const float curve = 1.0f + clampf (shape, 0.0f, 1.0f) * 6.0f;

        // Ducks hard on the beat and recovers over the rest of the period.
        const float env = std::pow (p, 1.0f / curve);
        return 1.0f - clampf (depth, 0.0f, 1.0f) * (1.0f - env);
    }
};

/** Final safety stage: blocks DC, then soft-clips anything still over full scale.

    A synth that can self-oscillate its filter into a distortion chain needs a
    backstop, but it should never engage on a sane patch - below -0.5 dBFS this is
    a straight wire. */
class OutputStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        dcL.prepare (sampleRate);
        dcR.prepare (sampleRate);
        // Second stage at 18 Hz: the asymmetric drive shapes turn each note-on's
        // envelope step into a ~11 Hz thump that a single 12 Hz first-order pole
        // barely touches (measured at only 8 dB below a stab's body). Two stages
        // knock ~9 dB off the thump while costing under half a dB at 27.5 Hz, the
        // lowest sub the presets reach.
        dcL2.prepare (sampleRate, 18.0f);
        dcR2.prepare (sampleRate, 18.0f);
    }

    void reset() noexcept { dcL.reset(); dcR.reset(); dcL2.reset(); dcR2.reset(); }

    void process (float& l, float& r, float gain) noexcept
    {
        l = dcL2.process (dcL.process (l)) * gain;
        r = dcR2.process (dcR.process (r)) * gain;

        l = limit (l);
        r = limit (r);
    }

private:
    static float limit (float x) noexcept
    {
        constexpr float knee = 0.944f;   // -0.5 dBFS
        const float a = std::abs (x);
        if (a <= knee)
            return x;

        const float over = a - knee;
        const float shaped = knee + over / (1.0f + over * (1.0f / (1.0f - knee)));
        return x >= 0.0f ? shaped : -shaped;
    }

    DcBlocker dcL, dcR, dcL2, dcR2;
};

} // namespace nd
