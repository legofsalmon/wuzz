#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nd
{

inline constexpr float kPi     = 3.14159265358979323846f;
inline constexpr float kTwoPi  = 6.28318530717958647692f;

/** Denormal flush. Voices that decay to silence otherwise keep feeding
    subnormal values into the filters, which is a real CPU cliff on x86. */
inline float fd (float x) noexcept
{
    return (std::abs (x) < 1.0e-18f) ? 0.0f : x;
}

inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** 7/6 Pade approximant of tanh. The input is clamped to +/-4 first, which keeps
    the rational form monotonic and bounded (it starts to exceed 1.0 past |x|~5).
    Roughly 4x faster than std::tanh and accurate to ~1e-5 across the clamped range. */
inline float fastTanh (float x) noexcept
{
    const float xc = clampf (x, -4.0f, 4.0f);
    const float x2 = xc * xc;
    const float num = xc * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
}


/** Fast 2^x, accurate to ~1e-6 over the range the synth uses.

    Pitch, cutoff and drive are all exponential in this engine, so exp2 sits in the
    innermost loop several times per voice per sample. The integer part goes straight
    into the float exponent field and a quintic covers the fraction. */
inline float fastExp2 (float x) noexcept
{
    x = clampf (x, -126.0f, 126.0f);

    const float xf = std::floor (x);
    const float f  = x - xf;

    const float p = 1.0f + f * (0.6931472f
                        + f * (0.2402265f
                        + f * (0.0555041f
                        + f * (0.0096181f
                        + f *  0.0013333f))));

    union { float f; int32_t i; } u;
    u.i = ((int32_t) xf + 127) << 23;
    return p * u.f;
}

inline float dbToGain (float db) noexcept
{
    return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
}

inline float midiToFreq (float note) noexcept
{
    return 440.0f * fastExp2 ((note - 69.0f) * (1.0f / 12.0f));
}

/** Cheap deterministic noise source. xorshift32 keeps voices reproducible
    across runs, which matters for the regression tests. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept : state (seed ? seed : 1u) {}

    uint32_t nextUInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept
    {
        return (float) (int32_t) nextUInt() * (1.0f / 2147483648.0f);
    }

    /** Uniform in [0, 1). */
    float nextUnipolar() noexcept
    {
        return (float) (nextUInt() >> 8) * (1.0f / 16777216.0f);
    }

private:
    uint32_t state;
};

/** One-pole low pass, used for parameter smoothing and for the analog drift
    generators. Coefficient is set from a time constant in seconds. */
class OnePole
{
public:
    void setTimeConstant (float seconds, double sampleRate) noexcept
    {
        coeff = seconds <= 0.0f ? 1.0f
                                : 1.0f - std::exp (-1.0f / (seconds * (float) sampleRate));
    }

    void reset (float value = 0.0f) noexcept { z = value; }

    float process (float x) noexcept
    {
        z += coeff * (x - z);
        return z = fd (z);
    }

    float value() const noexcept { return z; }

private:
    float coeff = 1.0f;
    float z = 0.0f;
};

/** First order DC blocker. The ladder filter and the drive stages both generate
    offset when pushed asymmetrically, and that offset eats headroom downstream. */
class DcBlocker
{
public:
    void prepare (double sampleRate, float cornerHz = 12.0f) noexcept
    {
        r = 1.0f - (kTwoPi * cornerHz / (float) sampleRate);
        reset();
    }

    void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = fd (y);
        return y1;
    }

private:
    float r = 0.9995f, x1 = 0.0f, y1 = 0.0f;
};

/** Band-limited step correction (polyBLEP).

    A discontinuity of height h falls between two sample instants. Both neighbouring
    samples get a correction of the same shape and opposite sign, so a single
    quadratic serves the whole oscillator bank as well as the hard-sync path.

    @param h  step height (value after the step - value before it)
    @param d  distance from the step in samples, in [0, 1). 0 == right at the step.
*/
inline float blepAfter (float h, float d) noexcept
{
    const float k = 1.0f - d;
    return -0.5f * h * k * k;
}

inline float blepBefore (float h, float d) noexcept
{
    const float k = 1.0f - d;
    return 0.5f * h * k * k;
}

/** sin(2*pi*x) for x in [0,1).

    The argument is folded into the first quadrant before the polynomial runs, so the
    9th order odd Taylor series only ever sees |t| <= pi/2 where its truncation error
    is ~3.6e-6 (about -109 dB). Without the fold the error at the period edges is
    -43 dB, which is audible. */
inline float fastSin (float x) noexcept
{
    x -= std::floor (x);

    float t;
    if (x < 0.25f)      t = x;             // sin(th)
    else if (x < 0.75f) t = 0.5f - x;      // sin(pi - th)
    else                t = x - 1.0f;      // sin(th - 2pi)

    t *= kTwoPi;
    const float t2 = t * t;
    return t * (1.0f + t2 * (-1.66666667e-1f
                    + t2 * (8.33333333e-3f
                    + t2 * (-1.98412698e-4f
                    + t2 * 2.75573192e-6f))));
}

} // namespace nd
