#include "Presets.h"
#include "Params.h"

namespace ndp
{

namespace
{
    struct PV { const char* id; float value; };

    struct PresetDef
    {
        const char* name;
        const PV* values;
        int count;
    };

    // Choice indices, spelled out so the tables below stay readable.
    enum { SAW = 0, PULSE = 1, TRI = 2, SINE = 3 };
    enum { LP24 = 0, LP12 = 1, BP24 = 2, BP12 = 3, HP24 = 4 };
    enum { D_SOFT = 0, D_TUBE = 1, D_HARD = 2, D_FOLD = 3, D_FUZZ = 4 };
    enum { POLY = 0, MONO = 1, LEGATO = 2 };
    enum { SRC_NONE = 0, SRC_AMP = 1, SRC_MOD = 2, SRC_LFO1 = 3, SRC_LFO2 = 4,
           SRC_VEL = 5, SRC_KEY = 6, SRC_WHEEL = 7, SRC_AT = 8, SRC_RND = 9 };
    enum { DST_NONE = 0, DST_CUTOFF = 1, DST_RES = 2, DST_PITCH = 3, DST_OSC2PITCH = 4,
           DST_PW = 5, DST_DETUNE = 6, DST_O1LVL = 7, DST_O2LVL = 8, DST_SUBLVL = 9,
           DST_NOISE = 10, DST_PREDRV = 11, DST_POSTDRV = 12, DST_AMP = 13, DST_PAN = 14 };
    enum { LFO_SINE = 0, LFO_TRI = 1, LFO_SAWUP = 2, LFO_SAWDN = 3, LFO_SQR = 4, LFO_SH = 5, LFO_RND = 6 };

    // ---------------------------------------------------------------------------
    // The bank. These are built around the Nite Versions palette: detuned saws run
    // hot into a resonant ladder, short envelopes, and phaser on almost everything.
    // ---------------------------------------------------------------------------

    const PV pNiteBass[] = {
        { "osc1Wave", SAW }, { "osc1Level", 0.95f }, { "osc1Unison", 3 }, { "osc1Detune", 0.18f },
        { "osc1Spread", 0.25f }, { "osc1Octave", -1 },
        { "osc2Wave", SAW }, { "osc2Level", 0.55f }, { "osc2Octave", -1 }, { "osc2Fine", -9.0f },
        { "subWave", 2 }, { "subOctave", -1 }, { "subLevel", 0.5f },
        { "filterMode", LP24 }, { "cutoff", 210.0f }, { "resonance", 0.28f },
        { "filterDrive", 9.0f }, { "filterEnv", 0.42f }, { "keyTrack", 0.35f },
        { "ampA", 0.001f }, { "ampD", 0.5f }, { "ampS", 0.65f }, { "ampR", 0.12f },
        { "modA", 0.001f }, { "modD", 0.16f }, { "modS", 0.0f }, { "modR", 0.1f },
        { "preDriveType", D_TUBE }, { "preDrive", 7.0f },
        { "postDriveType", D_SOFT }, { "postDrive", 2.5f },
        { "voiceMode", MONO }, { "glide", 0.045f }, { "drift", 0.1f },
        { "outputGain", -7.0f }
    };

    const PV pRaveStab[] = {
        { "osc1Wave", SAW }, { "osc1Level", 0.9f }, { "osc1Unison", 7 }, { "osc1Detune", 0.42f },
        { "osc1Spread", 0.85f },
        { "osc2Wave", SAW }, { "osc2Level", 0.6f }, { "osc2Unison", 5 }, { "osc2Detune", 0.3f },
        { "osc2Spread", 0.7f }, { "osc2Coarse", -12.0f },
        { "filterMode", LP24 }, { "cutoff", 900.0f }, { "resonance", 0.4f },
        { "filterDrive", 6.0f }, { "filterEnv", 0.72f }, { "velToCutoff", 0.35f },
        { "ampA", 0.001f }, { "ampD", 0.32f }, { "ampS", 0.0f }, { "ampR", 0.18f },
        { "modA", 0.001f }, { "modD", 0.2f }, { "modS", 0.0f }, { "modR", 0.15f },
        { "preDriveType", D_HARD }, { "preDrive", 5.0f },
        { "postDriveType", D_TUBE }, { "postDrive", 3.5f },
        { "phaserOn", 1 }, { "phaserStages", 6 }, { "phaserRate", 0.5f }, { "phaserDepth", 0.75f },
        { "phaserFeedback", 0.65f }, { "phaserMix", 0.45f },
        { "numVoices", 10 }, { "outputGain", -9.0f }
    };

    const PV pAcidDrive[] = {
        { "osc1Wave", SAW }, { "osc1Level", 1.0f }, { "osc1Unison", 1 },
        { "osc2Level", 0.0f }, { "subLevel", 0.18f }, { "subWave", 0 },
        { "filterMode", LP24 }, { "cutoff", 160.0f }, { "resonance", 0.86f },
        { "filterDrive", 14.0f }, { "filterEnv", 0.66f }, { "keyTrack", 0.3f },
        { "ampA", 0.001f }, { "ampD", 1.2f }, { "ampS", 0.85f }, { "ampR", 0.08f },
        { "modA", 0.001f }, { "modD", 0.28f }, { "modS", 0.0f }, { "modR", 0.2f },
        { "preDriveType", D_FUZZ }, { "preDrive", 6.0f },
        { "postDriveType", D_TUBE }, { "postDrive", 4.0f },
        { "voiceMode", LEGATO }, { "glide", 0.06f },
        { "modSrc1", SRC_WHEEL }, { "modDst1", DST_CUTOFF }, { "modAmt1", 0.4f },
        { "modSrc2", SRC_VEL }, { "modDst2", DST_PREDRV }, { "modAmt2", 0.3f },
        { "delayOn", 1 }, { "delaySync", 1 }, { "delayDivision", 5 }, { "delayFeedback", 0.34f },
        { "delayMix", 0.2f }, { "delayPingPong", 1 },
        { "outputGain", -8.0f }
    };

    const PV pSupersawLead[] = {
        { "osc1Wave", SAW }, { "osc1Level", 0.85f }, { "osc1Unison", 7 }, { "osc1Detune", 0.34f },
        { "osc1Spread", 0.9f },
        { "osc2Wave", SAW }, { "osc2Level", 0.7f }, { "osc2Unison", 7 }, { "osc2Detune", 0.5f },
        { "osc2Spread", 1.0f }, { "osc2Octave", -1 },
        { "filterMode", LP24 }, { "cutoff", 3400.0f }, { "resonance", 0.22f },
        { "filterDrive", 4.0f }, { "filterEnv", 0.35f },
        { "ampA", 0.008f }, { "ampD", 1.5f }, { "ampS", 0.85f }, { "ampR", 0.35f },
        { "modA", 0.004f }, { "modD", 0.6f }, { "modS", 0.25f }, { "modR", 0.3f },
        { "preDriveType", D_SOFT }, { "preDrive", 3.0f },
        { "postDriveType", D_TUBE }, { "postDrive", 2.2f },
        { "ensembleOn", 1 }, { "ensembleRate", 0.5f }, { "ensembleDepth", 0.55f }, { "ensembleMix", 0.45f },
        { "phaserOn", 1 }, { "phaserRate", 0.22f }, { "phaserDepth", 0.6f }, { "phaserMix", 0.35f },
        { "lfo1Shape", LFO_TRI }, { "lfo1Rate", 5.2f },
        { "modSrc1", SRC_LFO1 }, { "modDst1", DST_PITCH }, { "modAmt1", 0.006f },
        { "modSrc2", SRC_WHEEL }, { "modDst2", DST_CUTOFF }, { "modAmt2", 0.35f },
        { "numVoices", 8 }, { "outputGain", -11.0f }
    };

    const PV pFuzzChords[] = {
        { "osc1Wave", PULSE }, { "osc1Level", 0.9f }, { "osc1PW", 0.32f }, { "osc1Unison", 3 },
        { "osc1Detune", 0.2f }, { "osc1Spread", 0.5f },
        { "osc2Wave", PULSE }, { "osc2Level", 0.65f }, { "osc2PW", 0.6f }, { "osc2Fine", 7.0f },
        { "filterMode", LP24 }, { "cutoff", 1500.0f }, { "resonance", 0.3f },
        { "filterDrive", 8.0f }, { "filterEnv", 0.4f },
        { "ampA", 0.002f }, { "ampD", 0.45f }, { "ampS", 0.45f }, { "ampR", 0.22f },
        { "modA", 0.001f }, { "modD", 0.25f }, { "modS", 0.0f }, { "modR", 0.2f },
        { "preDriveType", D_FUZZ }, { "preDrive", 9.0f },
        { "postDriveType", D_HARD }, { "postDrive", 3.0f },
        { "lfo2Shape", LFO_SINE }, { "lfo2Rate", 0.35f },
        { "modSrc1", SRC_LFO2 }, { "modDst1", DST_PW }, { "modAmt1", 0.3f },
        { "phaserOn", 1 }, { "phaserStages", 8 }, { "phaserRate", 0.3f }, { "phaserFeedback", 0.7f },
        { "phaserMix", 0.5f },
        { "numVoices", 8 }, { "outputGain", -11.0f }
    };

    const PV pSyncScream[] = {
        { "osc1Wave", SAW }, { "osc1Level", 0.35f }, { "osc1Unison", 1 },
        { "osc2Wave", SAW }, { "osc2Level", 0.95f }, { "osc2Unison", 1 }, { "osc2Sync", 1 },
        { "osc2Coarse", 7.0f },
        { "filterMode", LP24 }, { "cutoff", 6000.0f }, { "resonance", 0.2f },
        { "filterDrive", 5.0f }, { "filterEnv", 0.2f },
        { "ampA", 0.002f }, { "ampD", 0.5f }, { "ampS", 0.7f }, { "ampR", 0.2f },
        { "modA", 0.002f }, { "modD", 0.9f }, { "modS", 0.0f }, { "modR", 0.4f },
        { "modSrc1", SRC_MOD }, { "modDst1", DST_OSC2PITCH }, { "modAmt1", 0.45f },
        { "modSrc2", SRC_WHEEL }, { "modDst2", DST_OSC2PITCH }, { "modAmt2", 0.25f },
        { "preDriveType", D_TUBE }, { "preDrive", 5.0f },
        { "postDriveType", D_SOFT }, { "postDrive", 2.0f },
        { "voiceMode", MONO }, { "glide", 0.02f },
        { "outputGain", -10.0f }
    };

    const PV pRingMetal[] = {
        { "osc1Wave", SAW }, { "osc1Level", 0.6f },
        { "osc2Wave", SINE }, { "osc2Level", 0.5f }, { "osc2Coarse", 19.0f },
        { "ringLevel", 0.75f },
        { "filterMode", BP24 }, { "cutoff", 1400.0f }, { "resonance", 0.45f },
        { "filterDrive", 7.0f }, { "filterEnv", 0.5f },
        { "ampA", 0.001f }, { "ampD", 0.6f }, { "ampS", 0.2f }, { "ampR", 0.4f },
        { "modA", 0.001f }, { "modD", 0.3f }, { "modS", 0.0f }, { "modR", 0.3f },
        { "preDriveType", D_FOLD }, { "preDrive", 4.0f },
        { "postDriveType", D_TUBE }, { "postDrive", 3.0f },
        { "delayOn", 1 }, { "delaySync", 1 }, { "delayDivision", 6 }, { "delayFeedback", 0.45f },
        { "delayMix", 0.3f }, { "delayPingPong", 1 },
        { "outputGain", -12.0f }
    };

    const PV pJunoPad[] = {
        { "osc1Wave", PULSE }, { "osc1Level", 0.8f }, { "osc1PW", 0.5f }, { "osc1Unison", 2 },
        { "osc1Detune", 0.12f }, { "osc1Spread", 0.7f },
        { "osc2Wave", SAW }, { "osc2Level", 0.45f }, { "osc2Octave", -1 },
        { "subLevel", 0.2f },
        { "filterMode", LP24 }, { "cutoff", 1800.0f }, { "resonance", 0.15f },
        { "filterDrive", 2.0f }, { "filterEnv", 0.3f },
        { "ampA", 0.35f }, { "ampD", 1.5f }, { "ampS", 0.8f }, { "ampR", 0.9f },
        { "modA", 0.4f }, { "modD", 1.2f }, { "modS", 0.4f }, { "modR", 0.8f },
        { "lfo2Shape", LFO_SINE }, { "lfo2Rate", 0.22f },
        { "modSrc1", SRC_LFO2 }, { "modDst1", DST_PW }, { "modAmt1", 0.35f },
        { "ensembleOn", 1 }, { "ensembleRate", 0.45f }, { "ensembleDepth", 0.7f }, { "ensembleMix", 0.6f },
        { "numVoices", 8 }, { "outputGain", -12.0f }
    };

    const PV pPumpSaws[] = {
        { "osc1Wave", SAW }, { "osc1Level", 0.9f }, { "osc1Unison", 7 }, { "osc1Detune", 0.38f },
        { "osc1Spread", 0.8f },
        { "osc2Wave", SAW }, { "osc2Level", 0.5f }, { "osc2Octave", -1 }, { "osc2Unison", 3 },
        { "osc2Detune", 0.25f },
        { "filterMode", LP24 }, { "cutoff", 2200.0f }, { "resonance", 0.3f },
        { "filterDrive", 5.0f }, { "filterEnv", 0.45f },
        { "ampA", 0.004f }, { "ampD", 0.8f }, { "ampS", 0.75f }, { "ampR", 0.3f },
        { "modA", 0.002f }, { "modD", 0.4f }, { "modS", 0.1f }, { "modR", 0.25f },
        { "preDriveType", D_TUBE }, { "preDrive", 4.5f },
        { "postDriveType", D_SOFT }, { "postDrive", 2.0f },
        { "pumpOn", 1 }, { "pumpDepth", 0.65f }, { "pumpShape", 0.55f }, { "pumpDivision", 8 },
        { "delayOn", 1 }, { "delaySync", 1 }, { "delayDivision", 5 }, { "delayFeedback", 0.3f },
        { "delayMix", 0.22f },
        { "numVoices", 10 }, { "outputGain", -10.0f }
    };

    const PV pSubThump[] = {
        { "osc1Wave", TRI }, { "osc1Level", 0.5f }, { "osc1Octave", -1 },
        { "osc2Level", 0.0f },
        { "subWave", 0 }, { "subOctave", -1 }, { "subLevel", 0.9f },
        { "filterMode", LP24 }, { "cutoff", 320.0f }, { "resonance", 0.12f },
        { "filterDrive", 4.0f }, { "filterEnv", 0.55f },
        { "ampA", 0.001f }, { "ampD", 0.28f }, { "ampS", 0.0f }, { "ampR", 0.1f },
        { "modA", 0.001f }, { "modD", 0.09f }, { "modS", 0.0f }, { "modR", 0.08f },
        { "preDriveType", D_SOFT }, { "preDrive", 2.0f },
        { "voiceMode", MONO }, { "glide", 0.0f },
        { "outputGain", -7.0f }
    };

    const PV pNoiseSweep[] = {
        { "osc1Level", 0.0f }, { "osc2Level", 0.0f }, { "noiseLevel", 0.9f },
        { "filterMode", BP12 }, { "cutoff", 700.0f }, { "resonance", 0.75f },
        { "filterDrive", 3.0f }, { "filterEnv", 0.9f },
        { "ampA", 0.02f }, { "ampD", 3.0f }, { "ampS", 0.6f }, { "ampR", 1.2f },
        { "modA", 1.6f }, { "modD", 2.5f }, { "modS", 0.5f }, { "modR", 1.5f },
        { "phaserOn", 1 }, { "phaserRate", 0.15f }, { "phaserDepth", 0.9f }, { "phaserMix", 0.6f },
        { "numVoices", 4 }, { "outputGain", -14.0f }
    };

    const PV pInit[] = {
        { "osc1Wave", SAW }, { "osc1Level", 1.0f }, { "cutoff", 12000.0f },
        { "outputGain", -6.0f }
    };

    #define ND_PRESET(nm, arr) { nm, arr, (int) (sizeof (arr) / sizeof (PV)) }

    const PresetDef kPresets[] = {
        ND_PRESET ("Init",            pInit),
        ND_PRESET ("Nite Bass",       pNiteBass),
        ND_PRESET ("Rave Stab",       pRaveStab),
        ND_PRESET ("Acid Drive",      pAcidDrive),
        ND_PRESET ("Supersaw Lead",   pSupersawLead),
        ND_PRESET ("Fuzz Chords",     pFuzzChords),
        ND_PRESET ("Sync Scream",     pSyncScream),
        ND_PRESET ("Ring Metal",      pRingMetal),
        ND_PRESET ("Juno Pad",        pJunoPad),
        ND_PRESET ("Pump Saws",       pPumpSaws),
        ND_PRESET ("Sub Thump",       pSubThump),
        ND_PRESET ("Noise Sweep",     pNoiseSweep)
    };

    #undef ND_PRESET

    constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (PresetDef));
}

int getNumPresets() { return kNumPresets; }

juce::String getPresetName (int index)
{
    if (index < 0 || index >= kNumPresets)
        return {};

    return kPresets[index].name;
}

int applyPreset (juce::AudioProcessorValueTreeState& state, int index)
{
    if (index < 0 || index >= kNumPresets)
        return 0;

    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    const auto& preset = kPresets[index];
    int unresolved = 0;

    for (int i = 0; i < preset.count; ++i)
    {
        const auto& pv = preset.values[i];

        if (auto* p = state.getParameter (pv.id))
        {
            p->setValueNotifyingHost (p->convertTo0to1 (pv.value));
        }
        else
        {
            jassertfalse;   // preset references an id that is not in the layout
            ++unresolved;
        }
    }

    return unresolved;
}

} // namespace ndp
