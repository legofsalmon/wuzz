#pragma once

#include "DspUtil.h"
#include "Saturation.h"

namespace nd
{

enum class FilterMode
{
    LP24 = 0,
    LP12,
    BP24,
    BP12,
    HP24,
    NumModes
};

/** Zero-delay-feedback Moog ladder.

    Four TPT one-pole stages inside a resonant feedback loop. Because each stage is
    linear in its input, the loop is solved in closed form rather than with a unit
    delay, so the filter keeps its tuning and stays stable right up to
    self-oscillation instead of blowing up like a naive digital ladder.

    Writing stage i as y_i = G*x_i + d_i (d_i being the state feed-through), the
    chain gives y4 = G^4*u + B, and the feedback u = x - k*y4 solves to
    u = (x - k*B) / (1 + k*G^4).

    Drive saturates the loop input, which is where a real ladder distorts, and the
    resonance thins the bass exactly as the hardware does. */
class LadderFilter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        maxCutoff = std::min (20000.0f, 0.45f * sr);
        reset();
    }

    void reset() noexcept
    {
        s1 = s2 = s3 = s4 = 0.0f;
    }

    void setMode (FilterMode m) noexcept { mode = m; }

    /** Per-sample coefficients. Everything here depends only on the control inputs
        and the sample rate, never on the filter state, so a stereo pair shares one
        set - which also makes it structurally impossible for the two channels to be
        tuned differently, the bug this refactor exists to bury. */
    struct Coeffs
    {
        float G = 0.0f, k = 0.0f;
        float driveIn = 1.0f, driveInv = 1.0f, makeup = 1.5f;
    };

    Coeffs makeCoeffs (float cutoffHz, float resonance, float drive) noexcept
    {
        Coeffs c;
        c.G = poleGain (clampf (cutoffHz, 10.0f, maxCutoff));

        // 4 lets the loop self-oscillate; a touch beyond keeps it singing.
        c.k = clampf (resonance, 0.0f, 1.0f) * 4.2f;

        if (drive != cachedDrive)
        {
            cachedDrive = drive;
            const float d = clampf (drive, 1.0f, 60.0f);
            cachedDriveIn = d;
            cachedDriveInv = 1.0f / d;
            // Makeup restores the level the saturator takes away. It is applied to the
            // mixed output, which is downstream of the state updates, so it scales what
            // you hear without touching the loop gain that sets the tuning.
            cachedMakeup = d / ShapeMath::softClipUnity (d);
        }

        c.driveIn = cachedDriveIn;
        c.driveInv = cachedDriveInv;
        c.makeup = cachedMakeup;
        return c;
    }

    /** Runs one sample through this instance's state with prebuilt coefficients. */
    float processWith (const Coeffs& c, float x) noexcept
    {
        const float G = c.G;

        const float g1 = 1.0f - G;
        const float d1 = g1 * s1;
        const float d2 = g1 * s2;
        const float d3 = g1 * s3;
        const float d4 = g1 * s4;

        const float G2 = G * G;
        const float G3 = G2 * G;
        const float G4 = G3 * G;

        const float B = G3 * d1 + G2 * d2 + G * d3 + d4;

        float u = (x - c.k * B) / (1.0f + c.k * G4);

        // The ladder's own nonlinearity sits at the loop input.
        //
        // This one is deliberately memoryless, unlike the drive stages. Antiderivative
        // anti-aliasing effectively evaluates the shaper half a sample late, and half a
        // sample of extra delay inside a resonant feedback loop drags the resonant peak
        // flat - measured at -0.9% at 440 Hz rising to -12.8% at 10 kHz. Exact
        // self-oscillation tuning is worth more here than the ~2 dB of alias rejection
        // it would have bought, since the drive stages already dominate that number.
        u = ShapeMath::softClipUnity (u * c.driveIn) * c.driveInv;

        const float y1 = G * u  + d1;  s1 = fd (y1 + G * (u  - s1));
        const float y2 = G * y1 + d2;  s2 = fd (y2 + G * (y1 - s2));
        const float y3 = G * y2 + d3;  s3 = fd (y3 + G * (y2 - s3));
        const float y4 = G * y3 + d4;  s4 = fd (y4 + G * (y3 - s4));

        return mix (u, y1, y2, y3, y4) * c.makeup;
    }

    /** @param cutoffHz   in Hz, clamped internally
        @param resonance  0..1, where 1 is just into self-oscillation
        @param drive      loop drive, 1 = clean */
    float process (float x, float cutoffHz, float resonance, float drive) noexcept
    {
        return processWith (makeCoeffs (cutoffHz, resonance, drive), x);
    }

    /** Stereo pair through shared coefficients: the tan approximation, resonance
        scaling and drive cache run once instead of twice, and both channels are
        guaranteed identically tuned. Both filters must be prepared at the same rate;
        coefficients come from `left`. */
    static void processStereo (LadderFilter& left, LadderFilter& right,
                               float& l, float& r,
                               float cutoffHz, float resonance, float drive) noexcept
    {
        const Coeffs c = left.makeCoeffs (cutoffHz, resonance, drive);
        l = left.processWith (c, l);
        r = right.processWith (c, r);
    }

private:
    /** G = g/(1+g) with g = tan(pi*fc/sr).

        The 4 term Taylor series for tan is accurate to ~0.05% up to 0.65 rad. The
        voice runs oversampled, so even a 20 kHz cutoff stays inside that range. */
    float poleGain (float fc) const noexcept
    {
        const float x  = kPi * fc / sr;
        const float x2 = x * x;
        const float g  = x * (1.0f + x2 * (0.3333333f + x2 * (0.1333333f + x2 * 0.0539683f)));
        return g / (1.0f + g);
    }

    /** Classic ladder stage mixing. Tapping and summing the four stages gives the
        other responses without a second filter topology. */
    float mix (float u, float y1, float y2, float y3, float y4) const noexcept
    {
        switch (mode)
        {
            case FilterMode::LP24: return y4;
            case FilterMode::LP12: return y2;
            case FilterMode::BP24: return 4.0f * (y2 - 2.0f * y3 + y4);
            case FilterMode::BP12: return 2.0f * (y1 - y2);
            case FilterMode::HP24: return u - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4;
            default:               return y4;
        }
    }

    float sr = 48000.0f;
    float maxCutoff = 20000.0f;
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
    float cachedDrive = -1.0f, cachedDriveIn = 1.0f, cachedDriveInv = 1.0f, cachedMakeup = 1.5f;
    FilterMode mode = FilterMode::LP24;
};

/** One-pole high pass, used to keep sub content out of the drive stages and to trim
    the bottom of the master bus. */
class OnePoleHP
{
public:
    void prepare (double sampleRate) noexcept { sr = (float) sampleRate; reset(); }
    void reset() noexcept { z = 0.0f; }

    float process (float x, float fc) noexcept
    {
        const float g = clampf (kTwoPi * fc / sr, 0.0f, 1.0f);
        z = fd (z + g * (x - z));
        return x - z;
    }

private:
    float sr = 48000.0f, z = 0.0f;
};

} // namespace nd
