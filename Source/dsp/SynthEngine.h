#pragma once

#include "Voice.h"
#include <array>

namespace nd
{

/** Polyphonic voice manager.

    Note events are applied immediately; the host driving this splits its buffer at
    event boundaries so timing stays sample accurate. Everything here is allocation
    free and safe to call from the audio thread. */
class SynthEngine
{
public:
    static constexpr int kMaxVoices = 16;

    void prepare (double oversampledRate, int numVoices) noexcept
    {
        sr = (float) oversampledRate;
        voiceCount = std::clamp (numVoices, 1, kMaxVoices);

        for (int i = 0; i < kMaxVoices; ++i)
            voices[(size_t) i].prepare (oversampledRate, 0x9E3779B9u + (uint32_t) i * 2654435761u);

        allNotesOff();
    }

    void setNumVoices (int n) noexcept
    {
        const int v = std::clamp (n, 1, kMaxVoices);
        if (v == voiceCount)
            return;

        for (int i = v; i < voiceCount; ++i)
            voices[(size_t) i].reset();

        voiceCount = v;
    }

    void allNotesOff() noexcept
    {
        for (auto& v : voices)
            v.reset();

        heldCount = 0;
        sustainDown = false;
        sustainedCount = 0;
    }

    void setPitchBend (float normalised, int rangeSemis) noexcept
    {
        pitchBend = normalised * (float) rangeSemis;
    }

    void setModWheel (float v) noexcept { modWheel = clampf (v, 0.0f, 1.0f); }

    void setAftertouch (float v) noexcept
    {
        for (auto& voice : voices)
            voice.setPressure (v);
    }

    void setSustain (bool down, const EngineParams& p) noexcept
    {
        sustainDown = down;
        if (down)
            return;

        for (int i = 0; i < sustainedCount; ++i)
            releaseNote (sustained[(size_t) i], p);

        sustainedCount = 0;
    }

    void noteOn (int midiNote, float velocity, const EngineParams& p) noexcept
    {
        if (velocity <= 0.0f)
        {
            noteOff (midiNote, p);
            return;
        }

        pushHeld (midiNote, velocity);
        removeSustained (midiNote);

        if (p.voiceMode == VoiceMode::Poly)
        {
            Voice& v = allocateVoice (midiNote);
            const bool wasActive = v.isActive();
            v.startNote (midiNote, velocity, p, wasActive);
            return;
        }

        // Mono and legato both play on a single voice. Legato only retriggers the
        // envelopes when nothing else is already held, which is what makes a
        // bassline slide rather than restart.
        Voice& v = voices[0];
        const bool alreadySounding = v.isActive() && ! v.isReleasing();
        const bool legatoSlide = (p.voiceMode == VoiceMode::Legato) && alreadySounding && heldCount > 1;

        if (legatoSlide)
            v.glideTo (midiNote, velocity);
        else
            v.startNote (midiNote, velocity, p, alreadySounding);
    }

    void noteOff (int midiNote, const EngineParams& p) noexcept
    {
        removeHeld (midiNote);

        if (sustainDown)
        {
            pushSustained (midiNote);
            return;
        }

        releaseNote (midiNote, p);
    }

    /** Adds `numSamples` of output into L and R. Both run at the oversampled rate. */
    void render (const EngineParams& p, float* L, float* R, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float l = 0.0f, r = 0.0f;

            for (int v = 0; v < voiceCount; ++v)
                voices[(size_t) v].render (p, pitchBend, modWheel, l, r);

            L[i] += l;
            R[i] += r;
        }
    }

    int getActiveVoiceCount() const noexcept
    {
        int n = 0;
        for (int i = 0; i < voiceCount; ++i)
            if (voices[(size_t) i].isActive())
                ++n;
        return n;
    }

private:
    void releaseNote (int midiNote, const EngineParams& p) noexcept
    {
        if (p.voiceMode != VoiceMode::Poly)
        {
            Voice& v = voices[0];

            if (heldCount > 0)
            {
                // Fall back to the most recently held note still down.
                const int prev = held[(size_t) (heldCount - 1)];
                const float pv = heldVel[(size_t) (heldCount - 1)];

                if (v.isActive() && v.getNote() == midiNote)
                {
                    if (p.voiceMode == VoiceMode::Legato)
                        v.glideTo (prev, pv);
                    else
                        v.startNote (prev, pv, p, true);
                }
                return;
            }

            if (v.getNote() == midiNote || v.isActive())
                v.stopNote();

            return;
        }

        for (int i = 0; i < voiceCount; ++i)
        {
            Voice& v = voices[(size_t) i];
            if (v.isActive() && ! v.isReleasing() && v.getNote() == midiNote)
                v.stopNote();
        }
    }

    /** Free voice first; then the quietest released voice; then the oldest. A stolen
        voice keeps its oscillator phases and envelope level, so the reassignment
        ramps from where it was rather than snapping to zero and clicking. */
    Voice& allocateVoice (int midiNote) noexcept
    {
        for (int i = 0; i < voiceCount; ++i)
            if (! voices[(size_t) i].isActive())
                return voices[(size_t) i];

        int best = 0;
        float bestScore = 1.0e9f;

        for (int i = 0; i < voiceCount; ++i)
        {
            Voice& v = voices[(size_t) i];

            // Prefer re-using the same note, then quiet releasing voices, then age.
            float score = v.getEnvLevel();
            if (v.isReleasing()) score *= 0.25f;
            if (v.getNote() == midiNote) score *= 0.1f;
            score -= (float) v.getAge() * 1.0e-7f;

            if (score < bestScore)
            {
                bestScore = score;
                best = i;
            }
        }

        return voices[(size_t) best];
    }

    void pushHeld (int n, float v) noexcept
    {
        removeHeld (n);
        if (heldCount >= (int) held.size())
            return;

        held[(size_t) heldCount] = n;
        heldVel[(size_t) heldCount] = v;
        ++heldCount;
    }

    void removeHeld (int n) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
        {
            if (held[(size_t) i] != n)
                continue;

            for (int j = i; j < heldCount - 1; ++j)
            {
                held[(size_t) j] = held[(size_t) (j + 1)];
                heldVel[(size_t) j] = heldVel[(size_t) (j + 1)];
            }
            --heldCount;
            return;
        }
    }

    void pushSustained (int n) noexcept
    {
        for (int i = 0; i < sustainedCount; ++i)
            if (sustained[(size_t) i] == n)
                return;

        if (sustainedCount < (int) sustained.size())
            sustained[(size_t) sustainedCount++] = n;
    }

    void removeSustained (int n) noexcept
    {
        for (int i = 0; i < sustainedCount; ++i)
        {
            if (sustained[(size_t) i] != n)
                continue;

            for (int j = i; j < sustainedCount - 1; ++j)
                sustained[(size_t) j] = sustained[(size_t) (j + 1)];

            --sustainedCount;
            return;
        }
    }

    std::array<Voice, kMaxVoices> voices;
    int voiceCount = 8;
    float sr = 96000.0f;
    float pitchBend = 0.0f;
    float modWheel = 0.0f;

    std::array<int, 128> held {};
    std::array<float, 128> heldVel {};
    int heldCount = 0;

    std::array<int, 128> sustained {};
    int sustainedCount = 0;
    bool sustainDown = false;
};

} // namespace nd
