#pragma once

#include <JuceHeader.h>

namespace ndp
{

int getNumPresets();
juce::String getPresetName (int index);

/** Resets every parameter to its default, then applies the preset's overrides. Doing
    it in that order means a preset only has to state what it actually cares about,
    and loading one never inherits stray values from the previous patch. */
void applyPreset (juce::AudioProcessorValueTreeState& state, int index);

} // namespace ndp
