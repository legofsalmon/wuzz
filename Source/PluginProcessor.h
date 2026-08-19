#pragma once

#include <JuceHeader.h>

#include "Params.h"
#include "dsp/Effects.h"
#include "dsp/SynthEngine.h"

/** NITEDRIVE - a two-oscillator supersaw synth built around a driven ladder filter.

    The audio path is: engine (oversampled) -> decimate -> phaser -> ensemble ->
    delay -> pump -> output stage. Only the voices are oversampled; the effects are
    either linear or slow enough that base rate is fine, and running them at 2x would
    double the cost of the delay lines for nothing. */
class NitedriveProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater
{
public:
    NitedriveProcessor();
    // Cancel here rather than relying on the base destructor: this object's members
    // are gone by the time ~AsyncUpdater runs.
    ~NitedriveProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;   // keep the double-precision overload visible

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Peak level of the last block, for the editor's meter. Written on the audio
        thread, read on the message thread; a torn read costs one stale meter frame. */
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<int>   activeVoices { 0 };

private:
    void refreshEngineParams();
    void applyMidiEvent (const juce::MidiMessage&);
    void renderEngine (float* left, float* right, int numBaseSamples, juce::MidiBuffer& midi);
    void setQuality (int index);
    void handleAsyncUpdate() override;

    ndp::Refs refs;
    nd::EngineParams params;
    nd::SynthEngine engine;

    nd::Phaser phaser;
    nd::Ensemble ensemble;
    nd::StereoDelay delay;
    nd::OutputStage output;

    /** Both oversamplers are built up front in prepareToPlay so that switching
        Quality mid-playback is a pointer swap rather than an allocation on the audio
        thread. Index 1 is 2x, index 2 is 4x; Eco bypasses them entirely. */
    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[3];
    juce::dsp::Oversampling<float>* oversampler = nullptr;
    std::atomic<int> pendingLatency { 0 };
    int oversamplingFactor = 1;
    int currentQuality = -1;
    double baseSampleRate = 48000.0;
    int maxBlockSize = 512;

    juce::SmoothedValue<float> outputGain;

    // Transport state, refreshed per block and used by the tempo-locked stages.
    double hostBpm = 120.0;
    double hostPpq = 0.0;
    bool   hostPlaying = false;

    double pumpPhase = 0.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NitedriveProcessor)
};
