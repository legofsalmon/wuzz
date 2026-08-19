#pragma once

#include "DspUtil.h"

namespace nd
{

enum class ModSource
{
    None = 0, Env1, Env2, Lfo1, Lfo2, Velocity, KeyTrack, ModWheel, Aftertouch, Random,
    NumSources
};

enum class ModDest
{
    None = 0, Cutoff, Resonance, Pitch, Osc2Pitch, PulseWidth, Detune,
    Osc1Level, Osc2Level, SubLevel, NoiseLevel, PreDrive, PostDrive, Amp, Pan, Lfo1Rate,
    NumDests
};

inline constexpr int kNumModSlots = 6;

struct ModSlot
{
    ModSource source = ModSource::None;
    ModDest   dest   = ModDest::None;
    float     amount = 0.0f;    // -1..1
};

/** Per-voice modulation bus.

    Sources are collected once per sample into `values`, then every slot adds its
    scaled contribution to a destination. Destinations are plain offsets in each
    parameter's own natural unit (semitones for pitch, normalised 0..1 for the
    rest), which the voice adds to the patched value.

    Depth is intentionally applied at the destination rather than the source, so two
    slots can read one LFO at different depths. */
struct ModBus
{
    float values[(int) ModSource::NumSources] = {};
    float dest  [(int) ModDest::NumDests]     = {};

    void clearDest() noexcept
    {
        for (auto& d : dest)
            d = 0.0f;
    }

    void apply (const ModSlot* slots, int numSlots) noexcept
    {
        for (int i = 0; i < numSlots; ++i)
        {
            const auto& s = slots[i];
            if (s.source == ModSource::None || s.dest == ModDest::None || s.amount == 0.0f)
                continue;

            dest[(int) s.dest] += values[(int) s.source] * s.amount;
        }
    }

    float operator[] (ModDest d) const noexcept { return dest[(int) d]; }
};

} // namespace nd
