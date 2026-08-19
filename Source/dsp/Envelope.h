#pragma once

#include "DspUtil.h"

namespace nd
{

/** Analog-style ADSR.

    Each segment is an exponential heading for a target *beyond* where it stops,
    which is what an RC stage in a hardware envelope actually does. A linear attack
    sounds flat and slow on percussive material; this one has the snap that short
    stabs need. The attack aims past 1.0 and the decay/release aim slightly below
    their end points, so both arrive in finite, predictable time.

    The curve of each segment is set by how far past the end point it aims: a large
    ratio is nearly linear, a small one is sharply curved. */
class AdsrEnv
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare (double sampleRate) noexcept
    {
        sr = (float) sampleRate;
        recalc();
        reset();
    }

    void reset() noexcept
    {
        stage = Stage::Idle;
        level = 0.0f;
    }

    /** @param a,d,r  seconds
        @param s      sustain level, 0..1 */
    void setParams (float a, float d, float s, float r) noexcept
    {
        if (a == atk && d == dcy && s == sus && r == rel)
            return;

        atk = a; dcy = d; sus = clampf (s, 0.0f, 1.0f); rel = r;
        recalc();
    }

    void noteOn() noexcept
    {
        if (killing)
        {
            killing = false;
            recalc();
        }

        stage = Stage::Attack;
    }

    void noteOff() noexcept
    {
        if (stage != Stage::Idle)
            stage = Stage::Release;
    }

    /** Fast fade for voice stealing, so a reassigned voice never clicks.

        The short time is held in a flag rather than written over `rel`, because the
        engine pushes the patch's release time in every block - which would otherwise
        cancel the fade a fraction of a millisecond after it started. */
    void kill() noexcept
    {
        killing = true;
        recalc();
        stage = Stage::Release;
    }

    bool isActive() const noexcept { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    float getLevel() const noexcept { return level; }

    float process() noexcept
    {
        switch (stage)
        {
            case Stage::Attack:
                level = attackBase + level * attackCoef;
                if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; }
                break;

            case Stage::Decay:
                level = decayBase + level * decayCoef;
                if (level <= sus) { level = sus; stage = Stage::Sustain; }
                break;

            case Stage::Sustain:
                level = sus;
                break;

            case Stage::Release:
                level = releaseBase + level * releaseCoef;
                if (level <= 1.0e-5f)
                {
                    level = 0.0f;
                    stage = Stage::Idle;
                    killing = false;
                }
                break;

            case Stage::Idle:
            default:
                level = 0.0f;
                break;
        }

        return level;
    }

private:
    /** Coefficient of an exponential that covers the segment in `seconds`, aiming
        `ratio` past the destination. */
    float coefFor (float seconds, float ratio) const noexcept
    {
        const float samples = std::max (1.0f, seconds * sr);
        return std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
    }

    void recalc() noexcept
    {
        // A gentle overshoot target keeps the attack punchy but not clicky; the
        // decay and release aim much closer, giving them a steeper analog curve.
        constexpr float ratioA  = 0.3f;
        constexpr float ratioDR = 0.0001f;

        attackCoef  = coefFor (atk, ratioA);
        attackBase  = (1.0f + ratioA) * (1.0f - attackCoef);

        decayCoef   = coefFor (dcy, ratioDR);
        decayBase   = (sus - ratioDR) * (1.0f - decayCoef);

        const float releaseTime = killing ? kKillSeconds : rel;
        releaseCoef = coefFor (releaseTime, ratioDR);
        releaseBase = -ratioDR * (1.0f - releaseCoef);
    }

    static constexpr float kKillSeconds = 0.005f;

    float sr = 48000.0f;
    float atk = 0.005f, dcy = 0.2f, sus = 0.7f, rel = 0.1f;
    float attackCoef = 0.0f, attackBase = 0.0f;
    float decayCoef = 0.0f, decayBase = 0.0f;
    float releaseCoef = 0.0f, releaseBase = 0.0f;
    float level = 0.0f;
    Stage stage = Stage::Idle;
    bool killing = false;
};

} // namespace nd
