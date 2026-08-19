#pragma once

#include "DspUtil.h"

namespace nd
{

enum class DriveType
{
    Soft = 0,   // cubic soft clip, the general purpose warmer
    Tube,       // biased, asymmetric, adds even harmonics
    Hard,       // clipper, buzzy and bright
    Fold,       // wavefolder, metallic
    Fuzz,       // fast knee, heavily compressed
    NumTypes
};

/** Closed forms for each shaper and its antiderivative.

    Every curve here was chosen to have an antiderivative in elementary arithmetic.
    tanh is the obvious soft clipper but its antiderivative is log(cosh(x)), and two
    transcendentals per sample per voice is not affordable. A cubic soft clip costs a
    couple of multiplies, has a polynomial antiderivative, and is arguably the more
    analog-sounding curve anyway. */
struct ShapeMath
{
    /** Tube bias. A real tube stage is asymmetric because it idles off-centre; the
        offset is what generates the even harmonics. */
    static constexpr float kTubeBias = 0.35f;

    static float f (DriveType t, float x) noexcept
    {
        switch (t)
        {
            case DriveType::Soft: return cubic (x);
            case DriveType::Tube: return cubic (x + kTubeBias) - cubic (kTubeBias);
            case DriveType::Hard: return clampf (x, -1.0f, 1.0f);
            case DriveType::Fold: return fastSin (x * 0.15915494f);   // sin(x), x/(2pi)
            case DriveType::Fuzz: return fuzz (x);
            default:              return x;
        }
    }

    static float F (DriveType t, float x) noexcept
    {
        switch (t)
        {
            case DriveType::Soft: return cubicAnti (x);
            case DriveType::Tube: return cubicAnti (x + kTubeBias) - cubic (kTubeBias) * x;
            case DriveType::Hard: return hardAnti (x);
            case DriveType::Fold: return -6.2831853f * fastCos (x * 0.15915494f);
            case DriveType::Fuzz: return fuzzAnti (x);
            default:              return 0.5f * x * x;
        }
    }

    /** Unity-slope cubic soft clip: f'(0) == 1, saturating at +/-2/3.

        The ladder needs this variant rather than the normalised one. Its feedback
        loop is designed around a unit-gain path, so a shaper with any other slope at
        the origin changes the effective resonance and drags the self-oscillation
        pitch off. */
    static float softClipUnity (float x) noexcept
    {
        if (x >= 1.0f)  return 2.0f / 3.0f;
        if (x <= -1.0f) return -2.0f / 3.0f;
        return x - x * x * x * (1.0f / 3.0f);
    }

private:
    /** Classic cubic soft clip, scaled so it saturates at exactly +/-1. */
    static float cubic (float x) noexcept
    {
        if (x >= 1.0f)  return 1.0f;
        if (x <= -1.0f) return -1.0f;
        return 1.5f * (x - x * x * x * (1.0f / 3.0f));
    }

    static float cubicAnti (float x) noexcept
    {
        const float a = std::abs (x);
        if (a >= 1.0f)
            return a - 0.375f;                       // continues linearly past the knee
        const float x2 = x * x;
        return 1.5f * (0.5f * x2 - x2 * x2 * (1.0f / 12.0f));
    }

    static float hardAnti (float x) noexcept
    {
        const float a = std::abs (x);
        return a >= 1.0f ? a - 0.5f : 0.5f * x * x;
    }

    /** Quadratic knee: reaches full saturation quickly, so it sounds fuzzier and more
        compressed than the cubic. */
    static float fuzz (float x) noexcept
    {
        const float a = std::abs (x);
        if (a >= 1.0f) return x >= 0.0f ? 1.0f : -1.0f;
        const float y = 2.0f * a - a * a;
        return x >= 0.0f ? y : -y;
    }

    static float fuzzAnti (float x) noexcept
    {
        const float a = std::abs (x);
        if (a >= 1.0f) return a - (1.0f / 3.0f);
        return a * a - a * a * a * (1.0f / 3.0f);
    }

    static float fastCos (float x) noexcept { return fastSin (x + 0.25f); }
};

/** A drive stage with first-order antiderivative anti-aliasing.

    A memoryless waveshaper generates harmonics far above Nyquist that fold straight
    back into the audible band; measured on this engine, drive was aliasing about
    50 dB worse than the oscillators feeding it. Rather than brute-force the whole
    voice to 8x oversampling, each stage integrates the shaper across the sample
    interval:

        y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])

    which is the average of f over that interval rather than a point sample of it.
    When two consecutive inputs are nearly equal the quotient loses precision, so it
    falls back to evaluating f at the midpoint. */
class DriveStage
{
public:
    void reset() noexcept
    {
        xPrev = 0.0f;
        fPrev = 0.0f;
        primed = false;
    }

    float process (DriveType type, float x, float drive) noexcept
    {
        const float d = clampf (drive, 1.0f, 60.0f);

        if (d != cachedDrive || type != cachedType)
        {
            cachedDrive = d;
            cachedType = type;
            // Keeps the drive knob a timbre control rather than a volume control.
            outScale = std::pow (d, -0.3f);
            reset();
        }

        const float xs = x * d;
        const float Fs = ShapeMath::F (type, xs);

        float y;
        const float diff = xs - xPrev;

        if (! primed || std::abs (diff) < 1.0e-5f)
            y = ShapeMath::f (type, 0.5f * (xs + xPrev));
        else
            y = (Fs - fPrev) / diff;

        xPrev = xs;
        fPrev = Fs;
        primed = true;

        return y * outScale;
    }

private:
    float xPrev = 0.0f, fPrev = 0.0f;
    float cachedDrive = -1.0f, outScale = 1.0f;
    DriveType cachedType = DriveType::NumTypes;
    bool primed = false;
};

} // namespace nd
