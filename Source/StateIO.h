#pragma once

#include <JuceHeader.h>

namespace ndp
{

/** Plugin state serialisation.

    Lives here rather than inline in the processor so the test suite exercises the
    same code the host does, including the program index that rides along with the
    parameter tree. */
inline void writeState (juce::AudioProcessorValueTreeState& state,
                        int currentProgram,
                        juce::MemoryBlock& destination)
{
    auto tree = state.copyState();
    tree.setProperty ("program", currentProgram, nullptr);

    if (auto xml = tree.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, destination);
}

/** @returns true if the state was understood and applied. Leaves everything untouched
             when handed data from another plugin or a corrupt session. */
inline bool readState (juce::AudioProcessorValueTreeState& state,
                       const void* data,
                       int sizeInBytes,
                       int& currentProgram)
{
    auto xml = juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return false;

    currentProgram = (int) tree.getProperty ("program", currentProgram);
    state.replaceState (tree);
    return true;
}

} // namespace ndp
