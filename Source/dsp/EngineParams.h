#pragma once

#include "Filter.h"
#include "Lfo.h"
#include "ModMatrix.h"
#include "Oscillator.h"
#include "Saturation.h"

namespace nd
{

enum class VoiceMode { Poly = 0, Mono, Legato, NumModes };
enum class SubWave   { Sine = 0, Triangle, Square, NumWaves };

/** A flat snapshot of everything a voice needs, refreshed once per block from the
    parameter tree. Voices never touch the plugin's parameters directly - that keeps
    the audio thread free of atomics in the inner loop and makes the engine testable
    without any host at all. */
struct EngineParams
{
    struct OscParams
    {
        Wave  wave    = Wave::Saw;
        float level   = 1.0f;      // 0..1
        int   octave  = 0;         // -3..+3
        float coarse  = 0.0f;      // semitones, -24..24
        float fine    = 0.0f;      // cents, -100..100
        float pulseWidth = 0.5f;
        float detune  = 0.0f;      // 0..1, JP-8000 curve
        float spread  = 0.0f;      // 0..1 stereo width
        int   unison  = 1;         // 1..7
    };

    OscParams osc1, osc2;

    bool     osc2Sync   = false;
    SubWave  subWave    = SubWave::Sine;
    int      subOctave  = -1;      // -1 or -2
    float    subLevel   = 0.0f;
    float    noiseLevel = 0.0f;
    float    ringLevel  = 0.0f;

    FilterMode filterMode  = FilterMode::LP24;
    float      cutoff      = 12000.0f;   // Hz
    float      resonance   = 0.0f;       // 0..1
    float      filterDrive = 1.0f;       // 1..60
    float      keyTrack    = 0.0f;       // 0..1
    float      filterEnv   = 0.0f;       // -1..1, scaled to +/- 8 octaves
    float      velToCutoff = 0.0f;       // 0..1

    float ampA = 0.002f, ampD = 0.3f, ampS = 0.8f, ampR = 0.15f;
    float modA = 0.002f, modD = 0.3f, modS = 0.0f, modR = 0.15f;
    float velToAmp = 0.4f;               // 0..1

    LfoShape lfo1Shape = LfoShape::Sine, lfo2Shape = LfoShape::Triangle;
    float    lfo1Rate  = 4.0f, lfo2Rate = 0.5f;   // Hz (already resolved from sync)
    bool     lfo1Retrig = true, lfo2Retrig = false;

    DriveType preDriveType  = DriveType::Soft;
    float     preDrive      = 1.0f;      // 1..60
    DriveType postDriveType = DriveType::Tube;
    float     postDrive     = 1.0f;

    VoiceMode voiceMode = VoiceMode::Poly;
    float     glideTime = 0.0f;          // seconds
    bool      glideLegatoOnly = true;
    int       pitchBendRange = 2;        // semitones
    bool      randomPhase = true;
    float     analogDrift = 0.15f;       // 0..1, per-voice tuning wander

    ModSlot mod[kNumModSlots];
};

} // namespace nd
