#pragma once

#include "DspUtil.h"
#include "WaveTables.h"

namespace nd
{

enum class Wave
{
    Saw = 0,
    Pulse,
    Triangle,
    Sine,
    NumWaves
};

/** The pair of mips an oscillator reads this block. Resolved once per block by the
    caller, because picking a mip costs a log2 and every unison voice shares one. */
struct TableSet
{
    const WaveTables::Table* saw = nullptr;
    const WaveTables::Table* tri = nullptr;
    float incLo = 1.0f;     // this mip is valid for incLo <= inc < incHi
    float incHi = -1.0f;

    static TableSet forIncrement (float inc) noexcept
    {
        TableSet t;
        t.update (inc);
        return t;
    }

    /** Reselects the mip only when the increment leaves the current one. Cutoff and
        pitch modulation move the increment every sample but almost never across a
        third-of-an-octave boundary, so this keeps a log2 out of the voice loop. */
    void update (float inc) noexcept
    {
        if (inc >= incLo && inc < incHi)
            return;

        const auto& w = WaveTables::instance();
        const int idx = WaveTables::indexForIncrement (inc);
        saw = &w.saw (idx);
        tri = &w.tri (idx);
        incLo = idx == 0 ? 0.0f
                         : WaveTables::kIncBase * std::exp2 ((float) idx / (float) WaveTables::kTablesPerOctave);
        incHi = idx == WaveTables::kNumTables - 1
                         ? 1.0e9f
                         : WaveTables::kIncBase * std::exp2 ((float) (idx + 1) / (float) WaveTables::kTablesPerOctave);
    }
};

/** Band-limited wavetable oscillator.

    Saw and triangle come straight from the mip bank. Pulse is synthesised as the
    difference of two saws a pulse-width apart, which is exact and stays band-limited
    for any width:

        pulse(p, w) = saw(p - w) - saw(p) + 2w - 1

    Hard sync is the one discontinuity the tables cannot band-limit, since it is not
    periodic with the waveform, so it keeps the two-sided polyBLEP correction. */
class Osc
{
public:
    void reset (float startPhase = 0.0f) noexcept
    {
        phase = startPhase - std::floor (startPhase);
    }

    float getPhase() const noexcept { return phase; }

    /** Samples since the last wrap, or -1 if that was more than a sample ago. */
    float wrapSince (float inc) const noexcept
    {
        const float u = phase / inc;
        return u < 1.0f ? u : -1.0f;
    }

    /** Samples until the next wrap, or -1 if it is more than a sample away. */
    float wrapUntil (float inc) const noexcept
    {
        const float s = (1.0f - phase) / inc;
        return s < 1.0f ? s : -1.0f;
    }

    /** Renders one sample and advances the phase.

        @param t          mips for this block
        @param inc        phase increment (freq / sampleRate), in (0, 0.5)
        @param wave       waveform
        @param pw         pulse width in (0,1); only Wave::Pulse uses it
        @param syncSince  samples since a hard-sync reset, in [0,1), or -1
        @param syncUntil  samples until the next hard-sync reset, in [0,1), or -1
    */
    float process (const TableSet& t,
                   float inc,
                   Wave wave,
                   float pw,
                   float syncSince = -1.0f,
                   float syncUntil = -1.0f) noexcept
    {
        float corr = 0.0f;

        if (syncSince >= 0.0f)
        {
            const float restarted = syncSince * inc;
            const float before = sample (t, wrap01 (phase + inc), wave, pw);
            const float after  = sample (t, restarted, wave, pw);
            phase = restarted;
            corr += blepAfter (after - before, syncSince);
        }
        else
        {
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }

        if (syncUntil >= 0.0f)
        {
            const float before = sample (t, wrap01 (phase + syncUntil * inc), wave, pw);
            const float after  = sample (t, 0.0f, wave, pw);
            corr += blepBefore (after - before, syncUntil);
        }

        return sample (t, phase, wave, pw) + corr;
    }

private:
    static float wrap01 (float p) noexcept
    {
        p -= std::floor (p);
        return p;
    }

    static float sample (const TableSet& t, float ph, Wave wave, float pw) noexcept
    {
        switch (wave)
        {
            case Wave::Saw:
                return WaveTables::read (*t.saw, ph);

            case Wave::Pulse:
            {
                const float w = clampf (pw, 0.02f, 0.98f);
                return WaveTables::read (*t.saw, wrap01 (ph - w))
                     - WaveTables::read (*t.saw, ph)
                     + (2.0f * w - 1.0f);
            }

            case Wave::Triangle:
                return WaveTables::read (*t.tri, ph);

            case Wave::Sine:
                return fastSin (ph);

            default:
                return 0.0f;
        }
    }

    float phase = 0.0f;
};

} // namespace nd
