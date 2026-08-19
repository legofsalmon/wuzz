#pragma once

#include "EngineParams.h"
#include "Envelope.h"
#include "Unison.h"

namespace nd
{

/** One polyphonic voice: two unison stacks, a sub, noise, a ring modulator, the
    ladder, two drive stages and two envelopes.

    The whole voice runs at the oversampled rate. That costs oscillator cycles but it
    is the only way the drive stages and the resonant filter stay clean when pushed,
    and pushing them is the entire point of this instrument. */
class Voice
{
public:
    void prepare (double oversampledRate, uint32_t seed) noexcept
    {
        sr = (float) oversampledRate;
        rng = Rng (seed);
        filter.prepare (oversampledRate);
        ampEnv.prepare (oversampledRate);
        modEnv.prepare (oversampledRate);
        lfo1.prepare (oversampledRate, seed * 2654435761u + 1u);
        lfo2.prepare (oversampledRate, seed * 40503u + 7u);
        dcBlock.prepare (oversampledRate);
        driftLfo.setTimeConstant (0.35f, oversampledRate);
        glideSmoother = 0.0f;
        reset();
    }

    void reset() noexcept
    {
        filter.reset();
        ampEnv.reset();
        modEnv.reset();
        dcBlock.reset();
        dcBlockR.reset();
        preDriveL.reset(); preDriveR.reset();
        postDriveL.reset(); postDriveR.reset();
        active = false;
        note = -1;
    }

    bool isActive() const noexcept { return active; }
    bool isReleasing() const noexcept { return ampEnv.isReleasing(); }
    int  getNote() const noexcept { return note; }
    int  getAge() const noexcept { return age; }
    float getEnvLevel() const noexcept { return ampEnv.getLevel(); }

    void startNote (int midiNote, float velocity, const EngineParams& p, bool retrigger) noexcept
    {
        note = midiNote;
        vel = clampf (velocity, 0.0f, 1.0f);
        age = 0;
        active = true;

        targetNote = (float) midiNote;

        const bool glideFromExisting = retrigger && p.glideTime > 0.0f;
        if (! glideFromExisting)
            glideSmoother = targetNote;

        randomPerNote = rng.nextBipolar();
        driftTarget = rng.nextBipolar();

        if (! retrigger)
        {
            osc1.reset (rng, p.randomPhase);
            osc2.reset (rng, p.randomPhase);
            subOsc.reset (p.randomPhase ? rng.nextUnipolar() : 0.0f);
            syncPhase = 0.0f;
            filter.reset();
            dcBlock.reset();
        }

        osc1.setSize (p.osc1.unison);
        osc2.setSize (p.osc2.unison);

        if (! retrigger || p.lfo1Retrig) lfo1.reset (0.0f);
        if (! retrigger || p.lfo2Retrig) lfo2.reset (0.0f);

        ampEnv.noteOn();
        modEnv.noteOn();
    }

    void stopNote() noexcept
    {
        ampEnv.noteOff();
        modEnv.noteOff();
    }

    /** Quick fade used when this voice is stolen. */
    void steal() noexcept
    {
        ampEnv.kill();
        modEnv.noteOff();
    }

    void setPressure (float v) noexcept { pressure = clampf (v, 0.0f, 1.0f); }

    /** Renders one oversampled sample and adds it to the stereo accumulators. */
    void render (const EngineParams& p,
                 float pitchBendSemis,
                 float modWheel,
                 float& outL,
                 float& outR) noexcept
    {
        if (! active)
            return;

        ++age;

        // ---- envelopes and LFOs -------------------------------------------------
        const float e1 = ampEnv.process();
        const float e2 = modEnv.process();

        bus.values[(int) ModSource::Env1]       = e1;
        bus.values[(int) ModSource::Env2]       = e2;
        bus.values[(int) ModSource::Velocity]   = vel;
        bus.values[(int) ModSource::KeyTrack]   = ((float) note - 60.0f) * (1.0f / 48.0f);
        bus.values[(int) ModSource::ModWheel]   = modWheel;
        bus.values[(int) ModSource::Aftertouch] = pressure;
        bus.values[(int) ModSource::Random]     = randomPerNote;

        bus.clearDest();
        // LFO 1's rate can itself be modulated, so its slot is applied first.
        bus.values[(int) ModSource::Lfo1] = 0.0f;
        bus.values[(int) ModSource::Lfo2] = lfo2.process (p.lfo2Rate);
        bus.apply (p.mod, kNumModSlots);

        const float lfo1Rate = clampf (p.lfo1Rate * std::exp2 (bus[ModDest::Lfo1Rate] * 4.0f),
                                       0.0f, 200.0f);
        bus.values[(int) ModSource::Lfo1] = lfo1.process (lfo1Rate);

        bus.clearDest();
        bus.apply (p.mod, kNumModSlots);

        if (! ampEnv.isActive())
        {
            active = false;
            return;
        }

        // ---- pitch --------------------------------------------------------------
        if (p.glideTime > 0.0f)
        {
            if (p.glideTime != cachedGlideTime)
            {
                cachedGlideTime = p.glideTime;
                glideCoef = 1.0f - std::exp (-1.0f / (p.glideTime * sr));
            }
            glideSmoother += glideCoef * (targetNote - glideSmoother);
        }
        else
        {
            glideSmoother = targetNote;
        }

        // Slow random detune per voice: the reason a real polysynth never sounds
        // like one oscillator played six times.
        const float drift = driftLfo.process (driftTarget) * p.analogDrift * 0.08f;
        if (age % 2048 == 0)
            driftTarget = rng.nextBipolar();

        const float basePitch = glideSmoother + pitchBendSemis + drift
                              + bus[ModDest::Pitch] * 24.0f;

        const float inc1 = incrementFor (basePitch, p.osc1);
        const float inc2 = incrementFor (basePitch + bus[ModDest::Osc2Pitch] * 24.0f, p.osc2);

        // ---- oscillators --------------------------------------------------------
        ts1.update (inc1);
        ts2.update (inc2);

        const float pwMod = bus[ModDest::PulseWidth] * 0.45f;
        const float pw1 = clampf (p.osc1.pulseWidth + pwMod, 0.03f, 0.97f);
        const float pw2 = clampf (p.osc2.pulseWidth + pwMod, 0.03f, 0.97f);

        const float detMod = bus[ModDest::Detune];
        const float det1 = clampf (p.osc1.detune + detMod, 0.0f, 1.0f);
        const float det2 = clampf (p.osc2.detune + detMod, 0.0f, 1.0f);

        float o1L = 0.0f, o1R = 0.0f, o2L = 0.0f, o2R = 0.0f;
        osc1.process (ts1, inc1, p.osc1.wave, pw1, det1, p.osc1.spread, o1L, o1R);

        // Hard sync: a dedicated master phase at osc1's base frequency drives the
        // reset, so sync works regardless of osc1's unison count.
        float syncSince = -1.0f, syncUntil = -1.0f;
        if (p.osc2Sync)
        {
            syncPhase += inc1;
            if (syncPhase >= 1.0f)
            {
                syncPhase -= 1.0f;
                syncSince = syncPhase / inc1;
            }
            const float until = (1.0f - syncPhase) / inc1;
            if (until < 1.0f)
                syncUntil = until;
        }

        osc2.processSynced (ts2, inc2, p.osc2.wave, pw2, det2, p.osc2.spread,
                            syncSince, syncUntil, o2L, o2R);

        const float g1 = levelGain (p.osc1.level, bus[ModDest::Osc1Level]);
        const float g2 = levelGain (p.osc2.level, bus[ModDest::Osc2Level]);

        o1L *= g1; o1R *= g1;
        o2L *= g2; o2R *= g2;

        // Sub and noise are mono; they anchor the centre of the image.
        float sub = 0.0f;
        const float subLvl = levelGain (p.subLevel, bus[ModDest::SubLevel]);
        if (subLvl > 0.0f)
        {
            const float subInc = clampf (inc1 * std::exp2 ((float) p.subOctave), 1.0e-6f, 0.45f);
            tsSub.update (subInc);
            const Wave  sw = p.subWave == SubWave::Sine     ? Wave::Sine
                           : p.subWave == SubWave::Triangle ? Wave::Triangle
                                                            : Wave::Pulse;
            sub = subOsc.process (tsSub, subInc, sw, 0.5f) * subLvl;
        }

        const float noiseLvl = levelGain (p.noiseLevel, bus[ModDest::NoiseLevel]);
        const float noise = noiseLvl > 0.0f ? rng.nextBipolar() * noiseLvl : 0.0f;

        // Ring modulation between the two stacks, summed to mono first so the
        // product stays centred rather than smearing the stereo field.
        float ringL = 0.0f, ringR = 0.0f;
        if (p.ringLevel > 0.0f)
        {
            ringL = o1L * o2L * 2.0f * p.ringLevel;
            ringR = o1R * o2R * 2.0f * p.ringLevel;
        }

        float l = o1L + o2L + sub + noise + ringL;
        float r = o1R + o2R + sub + noise + ringR;

        // ---- drive, filter, drive ----------------------------------------------
        const float preD  = driveAmount (p.preDrive,  bus[ModDest::PreDrive]);
        const float postD = driveAmount (p.postDrive, bus[ModDest::PostDrive]);

        if (preD > 1.001f)
        {
            l = preDriveL.process (p.preDriveType, l, preD);
            r = preDriveR.process (p.preDriveType, r, preD);
        }

        filter.setMode (p.filterMode);
        filterR.setMode (p.filterMode);

        const float cutoff = cutoffFor (p, e2, basePitch);
        const float res = clampf (p.resonance + bus[ModDest::Resonance], 0.0f, 1.0f);

        l = filter.process (l, cutoff, res, p.filterDrive);
        r = filterR.process (r, cutoff, res, p.filterDrive);

        if (postD > 1.001f)
        {
            l = postDriveL.process (p.postDriveType, l, postD);
            r = postDriveR.process (p.postDriveType, r, postD);
        }

        // ---- amp ----------------------------------------------------------------
        const float velGain = lerp (1.0f, vel, p.velToAmp);
        float amp = e1 * velGain * clampf (1.0f + bus[ModDest::Amp], 0.0f, 2.0f);

        l *= amp;
        r *= amp;

        // Asymmetric drive shapes leave an offset that would otherwise eat headroom.
        l = dcBlock.process (l);
        r = dcBlockR.process (r);

        const float pan = clampf (bus[ModDest::Pan], -1.0f, 1.0f);
        if (pan != 0.0f)
        {
            const float a = (pan + 1.0f) * 0.25f * kPi;
            const float cl = std::cos (a) * 1.41421356f;
            const float cr = std::sin (a) * 1.41421356f;
            l *= cl;
            r *= cr;
        }

        outL += l;
        outR += r;
    }

    /** Retargets an already-sounding voice, for mono and legato modes. */
    void glideTo (int midiNote, float velocity) noexcept
    {
        note = midiNote;
        targetNote = (float) midiNote;
        vel = clampf (velocity, 0.0f, 1.0f);
    }

private:
    static float levelGain (float base, float mod) noexcept
    {
        return clampf (base + mod, 0.0f, 1.0f);
    }

    static float driveAmount (float base, float mod) noexcept
    {
        return clampf (base * std::exp2 (mod * 3.0f), 1.0f, 60.0f);
    }

    float incrementFor (float pitch, const EngineParams::OscParams& o) const noexcept
    {
        const float n = pitch + (float) (o.octave * 12) + o.coarse + o.fine * 0.01f;
        return clampf (midiToFreq (n) / sr, 1.0e-7f, 0.45f);
    }

    float cutoffFor (const EngineParams& p, float env2, float pitch) const noexcept
    {
        // Everything that moves the cutoff does so in octaves, so a sweep sounds the
        // same wherever it starts.
        float oct = p.filterEnv * 8.0f * env2
                  + bus[ModDest::Cutoff] * 8.0f
                  + p.keyTrack * (pitch - 60.0f) * (1.0f / 12.0f)
                  + p.velToCutoff * vel * 4.0f;

        return clampf (p.cutoff * std::exp2 (oct), 10.0f, 0.45f * sr);
    }

    float sr = 96000.0f;
    bool  active = false;
    int   note = -1;
    int   age = 0;
    float vel = 1.0f;
    float pressure = 0.0f;
    float targetNote = 60.0f;
    float glideSmoother = 60.0f;
    float syncPhase = 0.0f;
    float randomPerNote = 0.0f;
    float driftTarget = 0.0f;
    float cachedGlideTime = -1.0f;
    float glideCoef = 1.0f;
    TableSet ts1, ts2, tsSub;

    UnisonStack osc1, osc2;
    Osc subOsc;
    LadderFilter filter, filterR;
    AdsrEnv ampEnv, modEnv;
    Lfo lfo1, lfo2;
    DcBlocker dcBlock, dcBlockR;
    DriveStage preDriveL, preDriveR, postDriveL, postDriveR;
    OnePole driftLfo;
    ModBus bus;
    Rng rng { 12345u };
};

} // namespace nd
