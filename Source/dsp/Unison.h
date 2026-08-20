#pragma once

#include "Oscillator.h"

namespace nd
{

/** Detuned oscillator stack - the supersaw.

    The detune spacing and the centre/side gain balance follow the Roland JP-8000
    curves measured by Adam Szabo. They matter more than they look: the spacing is
    deliberately irregular so the copies never line up into audible beating, and the
    detune knob is heavily curved so the musically useful narrow settings occupy most
    of its travel instead of the first two percent. */
class UnisonStack
{
public:
    static constexpr int kMaxVoices = 7;

    /** @param randomPhase  free-running start phases give the analog shimmer;
                            aligned phases give a bass patch a consistent attack. */
    void reset (Rng& rng, bool randomPhase, float fixedPhase = 0.0f) noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
            oscs[i].reset (randomPhase ? rng.nextUnipolar() : fixedPhase);
    }

    void setSize (int n) noexcept { count = n < 1 ? 1 : (n > kMaxVoices ? kMaxVoices : n); }
    int getSize() const noexcept { return count; }

    /** Renders the whole stack.

        @param baseInc  centre phase increment
        @param detune   0..1, mapped through the JP-8000 curve
        @param spread   0..1 stereo width
        @param outL/outR summed stereo output */
    void process (const TableSet& t,
                  float baseInc,
                  Wave wave,
                  float pw,
                  float detune,
                  float spread,
                  float& outL,
                  float& outR) noexcept
    {
        processSynced (t, baseInc, wave, pw, detune, spread, -1.0f, -1.0f, outL, outR);
    }

    /** As process(), but every oscillator in the stack is hard-synced to a common
        master. The stack keeps its detune, so sync sweeps still get the chorusing. */
    void processSynced (const TableSet& t,
                        float baseInc,
                        Wave wave,
                        float pw,
                        float detune,
                        float spread,
                        float syncSince,
                        float syncUntil,
                        float& outL,
                        float& outR) noexcept
    {
        if (count == 1)
        {
            const float v = oscs[0].process (t, baseInc, wave, pw, syncSince, syncUntil);
            outL += v;
            outR += v;
            return;
        }

        const float d  = detuneCurve (clampf (detune, 0.0f, 1.0f));
        const float mix = clampf (detune, 0.0f, 1.0f);
        const float gC = centreGain (mix);
        const float gS = sideGain (mix);

        updatePan (spread);

        const int* idx = layoutFor (count);

        // An even-sized stack has no oscillator sitting on the centre spread, so the
        // JP-8000 centre gain would go unclaimed and every voice would take the much
        // smaller side gain - the whole stack drops by over 20 dB at low detune, and
        // the level zigzags as the Unison knob is stepped. Splitting the centre gain
        // across the innermost pair keeps the series smooth and leaves odd sizes
        // bit-identical.
        const bool even = (count % 2) == 0;
        const int  innerA = even ? count / 2 - 1 : -1;
        const int  innerB = even ? count / 2     : -1;

        for (int i = 0; i < count; ++i)
        {
            const float off = kSpread[idx[i]];
            const float inc = clampf (baseInc * (1.0f + off * d), 1.0e-6f, 0.45f);
            // The inner pair replaces a centre voice AND its own two side slots, and
            // detuned copies add in power, not amplitude - a plain gC/2 leaves even
            // counts ~4.5 dB under their odd neighbours at working detunes. The
            // power-correct blend is exact for decorrelated copies and still lands on
            // gC/2 near zero detune where gS is negligible.
            const float g   = (off == 0.0f)              ? gC
                            : (i == innerA || i == innerB) ? std::sqrt (0.5f * gC * gC + gS * gS)
                                                           : gS;

            const float v = oscs[i].process (t, inc, wave, pw, syncSince, syncUntil) * g;

            outL += v * panL[i];
            outR += v * panR[i];
        }
    }

private:
    /** JP-8000 detune spacing. Irregular on purpose. */
    static constexpr float kSpread[kMaxVoices] = {
        -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f
    };

    /** Symmetric subsets of the 7-voice spacing, widest pair kept at every size. */
    static const int* layoutFor (int n) noexcept
    {
        static constexpr int l1[] = { 3 };
        static constexpr int l2[] = { 0, 6 };
        static constexpr int l3[] = { 0, 3, 6 };
        static constexpr int l4[] = { 0, 2, 4, 6 };
        static constexpr int l5[] = { 0, 1, 3, 5, 6 };
        static constexpr int l6[] = { 0, 1, 2, 4, 5, 6 };
        static constexpr int l7[] = { 0, 1, 2, 3, 4, 5, 6 };
        static constexpr const int* table[] = { l1, l1, l2, l3, l4, l5, l6, l7 };
        return table[n];
    }

    /** The JP-8000 detune response: an 11th order fit that spends most of the knob
        on the narrow settings where the supersaw actually lives. */
    static float detuneCurve (float x) noexcept
    {
        return (((((((((( 10028.7312891634f  * x - 50818.8652045924f) * x
                                             + 111363.4808729368f) * x
                                             - 138150.6761080548f) * x
                                             + 106649.6679158292f) * x
                                             - 53046.9642751875f)  * x
                                             + 17019.9518580080f)  * x
                                             - 3425.0836591318f)   * x
                                             + 404.2703938388f)    * x
                                             - 24.1878824391f)     * x
                                             + 0.6717417634f)      * x
                                             + 0.0030115596f;
    }

    /** Constant-power pan positions follow the detune direction, so the stack widens
        outward. They depend only on the stack size and the width knob, so they are
        rebuilt on change rather than recomputed per sample. */
    void updatePan (float spread) noexcept
    {
        if (spread == cachedSpread && count == cachedCount)
            return;

        cachedSpread = spread;
        cachedCount = count;

        const int* idx = layoutFor (count);
        for (int i = 0; i < count; ++i)
        {
            const float pos = clampf (kSpread[idx[i]] * (1.0f / 0.11f) * spread, -1.0f, 1.0f);
            const float a   = (pos + 1.0f) * 0.25f * kPi;      // 0..pi/2

            // Normalised so a centred voice is unity in both channels, matching the
            // voice-level panner. Without it a single-oscillator stack (which skips
            // this path entirely) sits 3 dB above a two-oscillator one.
            panL[i] = std::cos (a) * 1.41421356f;
            panR[i] = std::sin (a) * 1.41421356f;
        }
    }

    static float centreGain (float x) noexcept { return -0.55366f * x + 0.99785f; }
    static float sideGain   (float x) noexcept { return -0.73764f * x * x + 1.2841f * x + 0.044372f; }

    Osc oscs[kMaxVoices];
    float panL[kMaxVoices] = {}, panR[kMaxVoices] = {};
    float cachedSpread = -1.0f;
    int cachedCount = -1;
    int count = 1;
};

} // namespace nd
