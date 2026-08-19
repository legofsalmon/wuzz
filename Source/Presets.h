#pragma once

#include <JuceHeader.h>

namespace ndp
{

int getNumPresets();
juce::String getPresetName (int index);

/** Resets every parameter to its default, then applies the preset's overrides. Doing
    it in that order means a preset only has to state what it actually cares about,
    and loading one never inherits stray values from the previous patch.

    @returns the number of parameter ids in the preset that do not exist in the
             layout. Zero for a healthy bank; anything else means a typo that would
             otherwise be applied silently and leave the patch subtly wrong. */
int applyPreset (juce::AudioProcessorValueTreeState& state, int index);

} // namespace ndp
