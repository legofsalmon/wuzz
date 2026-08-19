#pragma once

#include <JuceHeader.h>
#include "dsp/EngineParams.h"

namespace ndp
{

/** Musical note divisions, in quarter-note beats. Shared by the tempo-synced LFOs,
    the delay and the pump so they all speak the same language. */
struct Division
{
    const char* name;
    float beats;
};

inline const Division kDivisions[] = {
    { "1/32",  0.125f },       { "1/16T", 1.0f / 6.0f },  { "1/16",  0.25f },
    { "1/8T",  1.0f / 3.0f },  { "1/16.", 0.375f },       { "1/8",   0.5f },
    { "1/4T",  2.0f / 3.0f },  { "1/8.",  0.75f },        { "1/4",   1.0f },
    { "1/2T",  4.0f / 3.0f },  { "1/4.",  1.5f },         { "1/2",   2.0f },
    { "1/1",   4.0f },         { "2/1",   8.0f }
};

inline constexpr int kNumDivisions = (int) (sizeof (kDivisions) / sizeof (kDivisions[0]));
inline constexpr int kDefaultDivision = 8;   // 1/4

juce::StringArray divisionNames();
juce::StringArray waveNames();
juce::StringArray subWaveNames();
juce::StringArray filterModeNames();
juce::StringArray lfoShapeNames();
juce::StringArray driveTypeNames();
juce::StringArray voiceModeNames();
juce::StringArray modSourceNames();
juce::StringArray modDestNames();
juce::StringArray qualityNames();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Cached raw parameter pointers.

    Resolved once at construction so the audio thread never touches the parameter
    tree by string. Every field is an atomic the host writes and the block-rate
    parameter refresh reads. */
struct Refs
{
    struct Osc
    {
        std::atomic<float>* wave = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* coarse = nullptr;
        std::atomic<float>* fine = nullptr;
        std::atomic<float>* pulseWidth = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* spread = nullptr;
        std::atomic<float>* unison = nullptr;
    };

    Osc osc1, osc2;
    std::atomic<float>* osc2Sync = nullptr;

    std::atomic<float>* subWave = nullptr;
    std::atomic<float>* subOctave = nullptr;
    std::atomic<float>* subLevel = nullptr;
    std::atomic<float>* noiseLevel = nullptr;
    std::atomic<float>* ringLevel = nullptr;

    std::atomic<float>* filterMode = nullptr;
    std::atomic<float>* cutoff = nullptr;
    std::atomic<float>* resonance = nullptr;
    std::atomic<float>* filterDrive = nullptr;
    std::atomic<float>* keyTrack = nullptr;
    std::atomic<float>* filterEnv = nullptr;
    std::atomic<float>* velToCutoff = nullptr;

    std::atomic<float>* ampA = nullptr;
    std::atomic<float>* ampD = nullptr;
    std::atomic<float>* ampS = nullptr;
    std::atomic<float>* ampR = nullptr;
    std::atomic<float>* velToAmp = nullptr;

    std::atomic<float>* modA = nullptr;
    std::atomic<float>* modD = nullptr;
    std::atomic<float>* modS = nullptr;
    std::atomic<float>* modR = nullptr;

    struct LfoRefs
    {
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* rate = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* retrig = nullptr;
    };

    LfoRefs lfo1, lfo2;

    std::atomic<float>* preDriveType = nullptr;
    std::atomic<float>* preDrive = nullptr;
    std::atomic<float>* postDriveType = nullptr;
    std::atomic<float>* postDrive = nullptr;

    std::atomic<float>* voiceMode = nullptr;
    std::atomic<float>* glide = nullptr;
    std::atomic<float>* glideLegato = nullptr;
    std::atomic<float>* bendRange = nullptr;
    std::atomic<float>* numVoices = nullptr;
    std::atomic<float>* randomPhase = nullptr;
    std::atomic<float>* drift = nullptr;
    std::atomic<float>* quality = nullptr;

    struct ModRefs
    {
        std::atomic<float>* source = nullptr;
        std::atomic<float>* dest = nullptr;
        std::atomic<float>* amount = nullptr;
    };

    ModRefs mod[nd::kNumModSlots];

    std::atomic<float>* phaserOn = nullptr;
    std::atomic<float>* phaserStages = nullptr;
    std::atomic<float>* phaserRate = nullptr;
    std::atomic<float>* phaserDepth = nullptr;
    std::atomic<float>* phaserCentre = nullptr;
    std::atomic<float>* phaserFeedback = nullptr;
    std::atomic<float>* phaserSpread = nullptr;
    std::atomic<float>* phaserMix = nullptr;

    std::atomic<float>* ensembleOn = nullptr;
    std::atomic<float>* ensembleRate = nullptr;
    std::atomic<float>* ensembleDepth = nullptr;
    std::atomic<float>* ensembleMix = nullptr;

    std::atomic<float>* delayOn = nullptr;
    std::atomic<float>* delaySync = nullptr;
    std::atomic<float>* delayTime = nullptr;
    std::atomic<float>* delayDivision = nullptr;
    std::atomic<float>* delayOffset = nullptr;
    std::atomic<float>* delayFeedback = nullptr;
    std::atomic<float>* delayTone = nullptr;
    std::atomic<float>* delayPingPong = nullptr;
    std::atomic<float>* delayMix = nullptr;

    std::atomic<float>* pumpOn = nullptr;
    std::atomic<float>* pumpDepth = nullptr;
    std::atomic<float>* pumpShape = nullptr;
    std::atomic<float>* pumpDivision = nullptr;

    std::atomic<float>* outputGain = nullptr;

    void bind (juce::AudioProcessorValueTreeState& state);
};

} // namespace ndp
