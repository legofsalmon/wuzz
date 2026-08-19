#include "Params.h"

namespace ndp
{

using APVTS = juce::AudioProcessorValueTreeState;
using Range = juce::NormalisableRange<float>;

juce::StringArray divisionNames()
{
    juce::StringArray a;
    for (const auto& d : kDivisions)
        a.add (d.name);
    return a;
}

juce::StringArray waveNames()       { return { "Saw", "Pulse", "Triangle", "Sine" }; }
juce::StringArray subWaveNames()    { return { "Sine", "Triangle", "Square" }; }
juce::StringArray filterModeNames() { return { "LP 24", "LP 12", "BP 24", "BP 12", "HP 24" }; }
juce::StringArray lfoShapeNames()   { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Random" }; }
juce::StringArray driveTypeNames()  { return { "Soft", "Tube", "Hard", "Fold", "Fuzz" }; }
juce::StringArray voiceModeNames()  { return { "Poly", "Mono", "Legato" }; }
juce::StringArray qualityNames()    { return { "Eco (1x)", "High (2x)", "Ultra (4x)" }; }

juce::StringArray modSourceNames()
{
    return { "None", "Amp Env", "Mod Env", "LFO 1", "LFO 2",
             "Velocity", "Key Track", "Mod Wheel", "Aftertouch", "Random" };
}

juce::StringArray modDestNames()
{
    return { "None", "Cutoff", "Resonance", "Pitch", "Osc 2 Pitch", "Pulse Width",
             "Detune", "Osc 1 Level", "Osc 2 Level", "Sub Level", "Noise Level",
             "Pre Drive", "Post Drive", "Amp", "Pan", "LFO 1 Rate" };
}

namespace
{
    /** Frequency-style range: fine control at the bottom where the ear is, coarse at
        the top. */
    Range freqRange (float lo, float hi, float centre)
    {
        Range r { lo, hi };
        r.setSkewForCentre (centre);
        return r;
    }

    Range timeRange (float lo, float hi, float centre)
    {
        Range r { lo, hi };
        r.setSkewForCentre (centre);
        return r;
    }

    Range unit() { return { 0.0f, 1.0f }; }

    juce::String pct (float v, int)      { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String hz  (float v, int)      { return v < 100.0f ? juce::String (v, 2) + " Hz"
                                                             : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String secs (float v, int)     { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                                                           : juce::String (v, 2) + " s"; }
    juce::String semis (float v, int)    { return juce::String (v, 2) + " st"; }
    juce::String cents (float v, int)    { return juce::String (juce::roundToInt (v)) + " ct"; }
    juce::String decibels (float v, int) { return juce::String (v, 1) + " dB"; }
    juce::String plain (float v, int)    { return juce::String (v, 2); }

    void addFloat (APVTS::ParameterLayout& l, juce::String id, juce::String name,
                   Range range, float def,
                   juce::String (*fmt) (float, int) = plain)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
    }

    void addChoice (APVTS::ParameterLayout& l, juce::String id, juce::String name,
                    juce::StringArray choices, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id, 1 }, name, choices, def));
    }

    void addBool (APVTS::ParameterLayout& l, juce::String id, juce::String name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id, 1 }, name, def));
    }

    void addInt (APVTS::ParameterLayout& l, juce::String id, juce::String name,
                 int lo, int hi, int def)
    {
        l.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id, 1 }, name, lo, hi, def));
    }

    /** Both oscillators expose the same controls, so they are declared once. */
    void addOsc (APVTS::ParameterLayout& l, const juce::String& p, const juce::String& label,
                 int defWave, float defLevel, int defOctave)
    {
        addChoice (l, p + "Wave",   label + " Wave", waveNames(), defWave);
        addFloat  (l, p + "Level",  label + " Level", unit(), defLevel, pct);
        addInt    (l, p + "Octave", label + " Octave", -3, 3, defOctave);
        addFloat  (l, p + "Coarse", label + " Coarse", { -24.0f, 24.0f, 1.0f }, 0.0f, semis);
        addFloat  (l, p + "Fine",   label + " Fine", { -100.0f, 100.0f }, 0.0f, cents);
        addFloat  (l, p + "PW",     label + " Pulse Width", { 0.02f, 0.98f }, 0.5f, pct);
        addFloat  (l, p + "Detune", label + " Detune", unit(), 0.25f, pct);
        addFloat  (l, p + "Spread", label + " Spread", unit(), 0.6f, pct);
        addInt    (l, p + "Unison", label + " Unison", 1, 7, 1);
    }

    void addLfo (APVTS::ParameterLayout& l, const juce::String& p, const juce::String& label,
                 int defShape, float defRate, bool defRetrig)
    {
        addChoice (l, p + "Shape",    label + " Shape", lfoShapeNames(), defShape);
        addFloat  (l, p + "Rate",     label + " Rate", freqRange (0.01f, 40.0f, 3.0f), defRate, hz);
        addBool   (l, p + "Sync",     label + " Sync", false);
        addChoice (l, p + "Division", label + " Division", divisionNames(), kDefaultDivision);
        addBool   (l, p + "Retrig",   label + " Retrigger", defRetrig);
    }
}

APVTS::ParameterLayout createLayout()
{
    APVTS::ParameterLayout l;

    addOsc (l, "osc1", "Osc 1", 0, 1.0f, 0);
    addOsc (l, "osc2", "Osc 2", 0, 0.0f, 0);
    addBool (l, "osc2Sync", "Osc 2 Sync", false);

    addChoice (l, "subWave",   "Sub Wave", subWaveNames(), 0);
    addInt    (l, "subOctave", "Sub Octave", -2, -1, -1);
    addFloat  (l, "subLevel",  "Sub Level", unit(), 0.0f, pct);
    addFloat  (l, "noiseLevel","Noise Level", unit(), 0.0f, pct);
    addFloat  (l, "ringLevel", "Ring Mod", unit(), 0.0f, pct);

    addChoice (l, "filterMode",  "Filter Mode", filterModeNames(), 0);
    addFloat  (l, "cutoff",      "Cutoff", freqRange (20.0f, 20000.0f, 1200.0f), 12000.0f, hz);
    addFloat  (l, "resonance",   "Resonance", unit(), 0.0f, pct);
    addFloat  (l, "filterDrive", "Filter Drive", freqRange (1.0f, 60.0f, 8.0f), 1.0f, plain);
    addFloat  (l, "keyTrack",    "Key Track", unit(), 0.0f, pct);
    addFloat  (l, "filterEnv",   "Filter Env", { -1.0f, 1.0f }, 0.0f, pct);
    addFloat  (l, "velToCutoff", "Vel > Cutoff", unit(), 0.0f, pct);

    addFloat (l, "ampA", "Amp Attack",  timeRange (0.0005f, 12.0f, 0.05f), 0.002f, secs);
    addFloat (l, "ampD", "Amp Decay",   timeRange (0.001f, 20.0f, 0.4f), 0.4f, secs);
    addFloat (l, "ampS", "Amp Sustain", unit(), 0.8f, pct);
    addFloat (l, "ampR", "Amp Release", timeRange (0.001f, 20.0f, 0.4f), 0.15f, secs);
    addFloat (l, "velToAmp", "Vel > Amp", unit(), 0.4f, pct);

    addFloat (l, "modA", "Mod Attack",  timeRange (0.0005f, 12.0f, 0.05f), 0.002f, secs);
    addFloat (l, "modD", "Mod Decay",   timeRange (0.001f, 20.0f, 0.4f), 0.35f, secs);
    addFloat (l, "modS", "Mod Sustain", unit(), 0.0f, pct);
    addFloat (l, "modR", "Mod Release", timeRange (0.001f, 20.0f, 0.4f), 0.2f, secs);

    addLfo (l, "lfo1", "LFO 1", 0, 4.0f, true);
    addLfo (l, "lfo2", "LFO 2", 1, 0.5f, false);

    addChoice (l, "preDriveType",  "Pre Drive Type", driveTypeNames(), 0);
    addFloat  (l, "preDrive",      "Pre Drive", freqRange (1.0f, 60.0f, 8.0f), 1.0f, plain);
    addChoice (l, "postDriveType", "Post Drive Type", driveTypeNames(), 1);
    addFloat  (l, "postDrive",     "Post Drive", freqRange (1.0f, 60.0f, 8.0f), 1.0f, plain);

    addChoice (l, "voiceMode",   "Voice Mode", voiceModeNames(), 0);
    addFloat  (l, "glide",       "Glide", timeRange (0.0f, 4.0f, 0.15f), 0.0f, secs);
    addBool   (l, "glideLegato", "Glide Legato Only", true);
    addInt    (l, "bendRange",   "Bend Range", 0, 24, 2);
    addInt    (l, "numVoices",   "Voices", 1, 16, 12);
    addBool   (l, "randomPhase", "Free Phase", true);
    addFloat  (l, "drift",       "Analog Drift", unit(), 0.15f, pct);
    addChoice (l, "quality",     "Quality", qualityNames(), 1);

    for (int i = 0; i < nd::kNumModSlots; ++i)
    {
        const auto n = juce::String (i + 1);
        addChoice (l, "modSrc" + n, "Mod " + n + " Source", modSourceNames(), 0);
        addChoice (l, "modDst" + n, "Mod " + n + " Dest", modDestNames(), 0);
        addFloat  (l, "modAmt" + n, "Mod " + n + " Amount", { -1.0f, 1.0f }, 0.0f, pct);
    }

    addBool  (l, "phaserOn",       "Phaser", false);
    addInt   (l, "phaserStages",   "Phaser Stages", 2, 12, 6);
    addFloat (l, "phaserRate",     "Phaser Rate", freqRange (0.01f, 12.0f, 0.5f), 0.35f, hz);
    addFloat (l, "phaserDepth",    "Phaser Depth", unit(), 0.7f, pct);
    addFloat (l, "phaserCentre",   "Phaser Centre", unit(), 0.45f, pct);
    addFloat (l, "phaserFeedback", "Phaser Feedback", { -0.95f, 0.95f }, 0.6f, pct);
    addFloat (l, "phaserSpread",   "Phaser Spread", unit(), 0.5f, pct);
    addFloat (l, "phaserMix",      "Phaser Mix", unit(), 0.5f, pct);

    addBool  (l, "ensembleOn",    "Ensemble", false);
    addFloat (l, "ensembleRate",  "Ensemble Rate", freqRange (0.05f, 8.0f, 0.8f), 0.6f, hz);
    addFloat (l, "ensembleDepth", "Ensemble Depth", unit(), 0.6f, pct);
    addFloat (l, "ensembleMix",   "Ensemble Mix", unit(), 0.5f, pct);

    addBool   (l, "delayOn",       "Delay", false);
    addBool   (l, "delaySync",     "Delay Sync", true);
    addFloat  (l, "delayTime",     "Delay Time", timeRange (0.01f, 3.0f, 0.35f), 0.35f, secs);
    addChoice (l, "delayDivision", "Delay Division", divisionNames(), 5);   // 1/8
    addFloat  (l, "delayOffset",   "Delay Offset", { -0.5f, 0.5f }, 0.0f, pct);
    addFloat  (l, "delayFeedback", "Delay Feedback", { 0.0f, 0.98f }, 0.35f, pct);
    addFloat  (l, "delayTone",     "Delay Tone", unit(), 0.6f, pct);
    addBool   (l, "delayPingPong", "Delay Ping Pong", false);
    addFloat  (l, "delayMix",      "Delay Mix", unit(), 0.25f, pct);

    addBool   (l, "pumpOn",       "Pump", false);
    addFloat  (l, "pumpDepth",    "Pump Depth", unit(), 0.5f, pct);
    addFloat  (l, "pumpShape",    "Pump Shape", unit(), 0.5f, pct);
    addChoice (l, "pumpDivision", "Pump Division", divisionNames(), kDefaultDivision);

    addFloat (l, "outputGain", "Output", { -40.0f, 12.0f }, -6.0f, decibels);

    return l;
}

void Refs::bind (APVTS& s)
{
    auto get = [&s] (const char* id) -> std::atomic<float>*
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);   // an id here that is not in createLayout()
        return p;
    };

    auto bindOsc = [&get] (Osc& o, const juce::String& p)
    {
        o.wave       = get ((p + "Wave").toRawUTF8());
        o.level      = get ((p + "Level").toRawUTF8());
        o.octave     = get ((p + "Octave").toRawUTF8());
        o.coarse     = get ((p + "Coarse").toRawUTF8());
        o.fine       = get ((p + "Fine").toRawUTF8());
        o.pulseWidth = get ((p + "PW").toRawUTF8());
        o.detune     = get ((p + "Detune").toRawUTF8());
        o.spread     = get ((p + "Spread").toRawUTF8());
        o.unison     = get ((p + "Unison").toRawUTF8());
    };

    bindOsc (osc1, "osc1");
    bindOsc (osc2, "osc2");
    osc2Sync = get ("osc2Sync");

    subWave    = get ("subWave");
    subOctave  = get ("subOctave");
    subLevel   = get ("subLevel");
    noiseLevel = get ("noiseLevel");
    ringLevel  = get ("ringLevel");

    filterMode  = get ("filterMode");
    cutoff      = get ("cutoff");
    resonance   = get ("resonance");
    filterDrive = get ("filterDrive");
    keyTrack    = get ("keyTrack");
    filterEnv   = get ("filterEnv");
    velToCutoff = get ("velToCutoff");

    ampA = get ("ampA"); ampD = get ("ampD"); ampS = get ("ampS"); ampR = get ("ampR");
    velToAmp = get ("velToAmp");
    modA = get ("modA"); modD = get ("modD"); modS = get ("modS"); modR = get ("modR");

    auto bindLfo = [&get] (LfoRefs& lr, const juce::String& p)
    {
        lr.shape    = get ((p + "Shape").toRawUTF8());
        lr.rate     = get ((p + "Rate").toRawUTF8());
        lr.sync     = get ((p + "Sync").toRawUTF8());
        lr.division = get ((p + "Division").toRawUTF8());
        lr.retrig   = get ((p + "Retrig").toRawUTF8());
    };

    bindLfo (lfo1, "lfo1");
    bindLfo (lfo2, "lfo2");

    preDriveType  = get ("preDriveType");
    preDrive      = get ("preDrive");
    postDriveType = get ("postDriveType");
    postDrive     = get ("postDrive");

    voiceMode   = get ("voiceMode");
    glide       = get ("glide");
    glideLegato = get ("glideLegato");
    bendRange   = get ("bendRange");
    numVoices   = get ("numVoices");
    randomPhase = get ("randomPhase");
    drift       = get ("drift");
    quality     = get ("quality");

    for (int i = 0; i < nd::kNumModSlots; ++i)
    {
        const auto n = juce::String (i + 1);
        mod[i].source = get (("modSrc" + n).toRawUTF8());
        mod[i].dest   = get (("modDst" + n).toRawUTF8());
        mod[i].amount = get (("modAmt" + n).toRawUTF8());
    }

    phaserOn       = get ("phaserOn");
    phaserStages   = get ("phaserStages");
    phaserRate     = get ("phaserRate");
    phaserDepth    = get ("phaserDepth");
    phaserCentre   = get ("phaserCentre");
    phaserFeedback = get ("phaserFeedback");
    phaserSpread   = get ("phaserSpread");
    phaserMix      = get ("phaserMix");

    ensembleOn    = get ("ensembleOn");
    ensembleRate  = get ("ensembleRate");
    ensembleDepth = get ("ensembleDepth");
    ensembleMix   = get ("ensembleMix");

    delayOn       = get ("delayOn");
    delaySync     = get ("delaySync");
    delayTime     = get ("delayTime");
    delayDivision = get ("delayDivision");
    delayOffset   = get ("delayOffset");
    delayFeedback = get ("delayFeedback");
    delayTone     = get ("delayTone");
    delayPingPong = get ("delayPingPong");
    delayMix      = get ("delayMix");

    pumpOn       = get ("pumpOn");
    pumpDepth    = get ("pumpDepth");
    pumpShape    = get ("pumpShape");
    pumpDivision = get ("pumpDivision");

    outputGain = get ("outputGain");
}

} // namespace ndp
