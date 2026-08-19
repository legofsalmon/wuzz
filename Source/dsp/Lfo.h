#pragma once

#include "DspUtil.h"

namespace nd
{

enum class LfoShape
{
    Sine = 0,
    Triangle,
    SawUp,
    SawDown,
    Square,
    SampleHold,
    SmoothRandom,
    NumShapes
};

/** Modulation LFO. Output is bipolar, -1..1.

    Sample & hold and smooth random are per-instance seeded so two LFOs never lock
    to the same random sequence, but they stay deterministic across runs so the
    regression tests can rely on them. */
class Lfo
{
public:
    void prepare (double sampleRate, uint32_t seed) noexcept
    {
        sr = (float) sampleRate;
        rng = Rng (seed);
        smoother.setTimeConstant (0.02f, sampleRate);
        reset (0.0f);
    }

    void reset (float startPhase = 0.0f) noexcept
    {
        phase = startPhase - std::floor (startPhase);
        held = rng.nextBipolar();
        target = rng.nextBipolar();
        smoother.reset (held);
    }

    void setShape (LfoShape s) noexcept { shape = s; }

    float process (float freqHz) noexcept
    {
        const float inc = clampf (freqHz, 0.0f, 0.45f * sr) / sr;

        phase += inc;
        bool wrapped = false;
        if (phase >= 1.0f) { phase -= 1.0f; wrapped = true; }

        switch (shape)
        {
            case LfoShape::Sine:     return fastSin (phase);
            case LfoShape::Triangle: return 4.0f * std::abs (phase - 0.5f) - 1.0f;
            case LfoShape::SawUp:    return 2.0f * phase - 1.0f;
            case LfoShape::SawDown:  return 1.0f - 2.0f * phase;
            case LfoShape::Square:   return phase < 0.5f ? 1.0f : -1.0f;

            case LfoShape::SampleHold:
                if (wrapped) held = rng.nextBipolar();
                return held;

            case LfoShape::SmoothRandom:
                if (wrapped) target = rng.nextBipolar();
                return smoother.process (target);

            default:
                return 0.0f;
        }
    }

private:
    float sr = 48000.0f;
    float phase = 0.0f;
    float held = 0.0f, target = 0.0f;
    LfoShape shape = LfoShape::Sine;
    Rng rng { 0x2545F491u };
    OnePole smoother;
};

} // namespace nd
