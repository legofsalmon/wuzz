#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "StateIO.h"

namespace
{
    float get (const std::atomic<float>* p, float fallback = 0.0f)
    {
        return p != nullptr ? p->load() : fallback;
    }

    bool getBool (const std::atomic<float>* p)
    {
        return p != nullptr && p->load() > 0.5f;
    }

    int getInt (const std::atomic<float>* p, int fallback = 0)
    {
        return p != nullptr ? (int) std::lround (p->load()) : fallback;
    }

    /** Clamps a choice index that came from a host into a valid enum value. */
    template <typename E>
    E choice (const std::atomic<float>* p, int count)
    {
        const int i = getInt (p);
        return (E) juce::jlimit (0, count - 1, i);
    }
}

NitedriveProcessor::NitedriveProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NITEDRIVE", ndp::createLayout())
{
    refs.bind (apvts);
}

bool NitedriveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void NitedriveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    phaser.prepare (sampleRate);
    ensemble.prepare (sampleRate);
    delay.prepare (sampleRate);
    output.prepare (sampleRate);

    monoScratch.setSize (2, maxBlockSize, false, false, true);
    monoScratch.clear();

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get (refs.outputGain, -6.0f)));

    for (int stages = 1; stages <= 2; ++stages)
    {
        auto os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) stages,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true, true);
        os->initProcessing ((size_t) maxBlockSize);
        os->reset();
        oversamplers[stages] = std::move (os);
    }

    currentQuality = -1;                     // force the first selection to take
    setQuality (getInt (refs.quality, 1));

    // Touching the table bank here means the first note never pays for building it.
    (void) nd::WaveTables::instance();
}

void NitedriveProcessor::setQuality (int index)
{
    const int q = juce::jlimit (0, 2, index);
    if (q == currentQuality)
        return;

    currentQuality = q;
    oversamplingFactor = 1 << q;             // 1x, 2x, 4x
    oversampler = q > 0 ? oversamplers[q].get() : nullptr;

    if (oversampler != nullptr)
        oversampler->reset();

    // Reporting latency touches the host, so it is deferred to the message thread;
    // this can be reached from processBlock when Quality is automated.
    pendingLatency.store (oversampler != nullptr
                              ? (int) std::round (oversampler->getLatencyInSamples())
                              : 0);
    triggerAsyncUpdate();

    // Allocation free: every voice member is a fixed-size array.
    engine.prepare (baseSampleRate * oversamplingFactor, getInt (refs.numVoices, 12));
}

void NitedriveProcessor::handleAsyncUpdate()
{
    setLatencySamples (pendingLatency.load());
}

void NitedriveProcessor::refreshEngineParams()
{
    auto osc = [] (nd::EngineParams::OscParams& o, const ndp::Refs::Osc& r)
    {
        o.wave       = choice<nd::Wave> (r.wave, (int) nd::Wave::NumWaves);
        o.level      = get (r.level);
        o.octave     = getInt (r.octave);
        o.coarse     = get (r.coarse);
        o.fine       = get (r.fine);
        o.pulseWidth = get (r.pulseWidth, 0.5f);
        o.detune     = get (r.detune);
        o.spread     = get (r.spread);
        o.unison     = juce::jlimit (1, 7, getInt (r.unison, 1));
    };

    osc (params.osc1, refs.osc1);
    osc (params.osc2, refs.osc2);
    params.osc2Sync = getBool (refs.osc2Sync);

    params.subWave    = choice<nd::SubWave> (refs.subWave, (int) nd::SubWave::NumWaves);
    params.subOctave  = getInt (refs.subOctave, -1);
    params.subLevel   = get (refs.subLevel);
    params.noiseLevel = get (refs.noiseLevel);
    params.ringLevel  = get (refs.ringLevel);

    params.filterMode  = choice<nd::FilterMode> (refs.filterMode, (int) nd::FilterMode::NumModes);
    params.cutoff      = get (refs.cutoff, 12000.0f);
    params.resonance   = get (refs.resonance);
    params.filterDrive = get (refs.filterDrive, 1.0f);
    params.keyTrack    = get (refs.keyTrack);
    params.filterEnv   = get (refs.filterEnv);
    params.velToCutoff = get (refs.velToCutoff);

    params.ampA = get (refs.ampA, 0.002f); params.ampD = get (refs.ampD, 0.4f);
    params.ampS = get (refs.ampS, 0.8f);   params.ampR = get (refs.ampR, 0.15f);
    params.velToAmp = get (refs.velToAmp, 0.4f);

    params.modA = get (refs.modA, 0.002f); params.modD = get (refs.modD, 0.35f);
    params.modS = get (refs.modS, 0.0f);   params.modR = get (refs.modR, 0.2f);

    // A synced LFO completes one cycle per division, so its rate follows the tempo.
    auto lfoRate = [this] (const ndp::Refs::LfoRefs& r) -> float
    {
        if (! getBool (r.sync))
            return get (r.rate, 1.0f);

        const int idx = juce::jlimit (0, ndp::kNumDivisions - 1, getInt (r.division, ndp::kDefaultDivision));
        const float beats = ndp::kDivisions[idx].beats;
        return (float) (hostBpm / (60.0 * (double) beats));
    };

    params.lfo1Shape = choice<nd::LfoShape> (refs.lfo1.shape, (int) nd::LfoShape::NumShapes);
    params.lfo2Shape = choice<nd::LfoShape> (refs.lfo2.shape, (int) nd::LfoShape::NumShapes);
    params.lfo1Rate = lfoRate (refs.lfo1);
    params.lfo2Rate = lfoRate (refs.lfo2);
    params.lfo1Retrig = getBool (refs.lfo1.retrig);
    params.lfo2Retrig = getBool (refs.lfo2.retrig);

    params.preDriveType  = choice<nd::DriveType> (refs.preDriveType, (int) nd::DriveType::NumTypes);
    params.preDrive      = get (refs.preDrive, 1.0f);
    params.postDriveType = choice<nd::DriveType> (refs.postDriveType, (int) nd::DriveType::NumTypes);
    params.postDrive     = get (refs.postDrive, 1.0f);

    params.voiceMode = choice<nd::VoiceMode> (refs.voiceMode, (int) nd::VoiceMode::NumModes);
    params.glideTime = get (refs.glide);
    params.glideLegatoOnly = getBool (refs.glideLegato);
    params.pitchBendRange = getInt (refs.bendRange, 2);
    params.randomPhase = getBool (refs.randomPhase);
    params.analogDrift = get (refs.drift, 0.15f);

    for (int i = 0; i < nd::kNumModSlots; ++i)
    {
        params.mod[i].source = choice<nd::ModSource> (refs.mod[i].source, (int) nd::ModSource::NumSources);
        params.mod[i].dest   = choice<nd::ModDest>   (refs.mod[i].dest,   (int) nd::ModDest::NumDests);
        params.mod[i].amount = get (refs.mod[i].amount);
    }

    engine.setNumVoices (getInt (refs.numVoices, 12));
}

void NitedriveProcessor::applyMidiEvent (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), params);
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber(), params);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
    else if (m.isPitchWheel())
        engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f, params.pitchBendRange);
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1)
            engine.setModWheel ((float) m.getControllerValue() / 127.0f);
        else if (m.getControllerNumber() == 64)
            engine.setSustain (m.getControllerValue() >= 64, params);
    }
    else if (m.isChannelPressure())
        engine.setAftertouch ((float) m.getChannelPressureValue() / 127.0f);
}

void NitedriveProcessor::renderEngine (float* left, float* right, int numBaseSamples,
                                       juce::MidiBuffer& midi)
{
    // Split the block at every event so note timing lands on the right sample rather
    // than being quantised to the block boundary - audible as flam on fast lines.
    int pos = 0;

    for (const auto meta : midi)
    {
        const int evt = juce::jlimit (0, numBaseSamples, meta.samplePosition);

        if (evt > pos)
        {
            engine.render (params,
                           left  + pos * oversamplingFactor,
                           right + pos * oversamplingFactor,
                           (evt - pos) * oversamplingFactor);
            pos = evt;
        }

        applyMidiEvent (meta.getMessage());
    }

    if (pos < numBaseSamples)
        engine.render (params,
                       left  + pos * oversamplingFactor,
                       right + pos * oversamplingFactor,
                       (numBaseSamples - pos) * oversamplingFactor);
}

void NitedriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0)
        return;

    // The oversampler's buffers are sized in prepareToPlay. A host is entitled to
    // hand over a larger block than it promised, and the oversampler would happily
    // run off the end of them, so anything oversized is split into chunks it can
    // take. Both the buffer view and the MIDI sub-range are allocation free.
    if (numSamples > maxBlockSize)
    {
        int offset = 0;

        while (offset < numSamples)
        {
            const int chunk = juce::jmin (maxBlockSize, numSamples - offset);

            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(), offset, chunk);

            chunkMidi.clear();
            for (const auto meta : midi)
            {
                const int pos = meta.samplePosition - offset;
                if (pos >= 0 && pos < chunk)
                    chunkMidi.addEvent (meta.getMessage(), pos);
            }

            processChunk (view, chunkMidi);
            offset += chunk;
        }

        midi.clear();
        return;
    }

    processChunk (buffer, midi);
}

void NitedriveProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            hostBpm = pos->getBpm().orFallback (hostBpm);
            hostPpq = pos->getPpqPosition().orFallback (hostPpq);
            hostPlaying = pos->getIsPlaying();
        }
    }

    setQuality (getInt (refs.quality, 1));
    refreshEngineParams();

    // A mono host still needs a valid second channel to render into. The scratch is
    // pre-allocated, so this is a view rather than an allocation.
    const bool mono = buffer.getNumChannels() < 2;
    juce::AudioBuffer<float> stereoView (monoScratch.getArrayOfWritePointers(), 2, 0, numSamples);
    juce::AudioBuffer<float>* work = &buffer;

    if (mono)
    {
        stereoView.clear();
        work = &stereoView;
    }

    juce::dsp::AudioBlock<float> block (*work);

    // Render the voices, oversampled. processSamplesUp runs the (silent) input
    // through the upsampler and hands back the oversampled block; the engine then
    // adds into it, and processSamplesDown applies the decimation filter that
    // removes everything the voices generated above the base Nyquist.
    if (oversampler != nullptr)
    {
        auto osBlock = oversampler->processSamplesUp (block);
        renderEngine (osBlock.getChannelPointer (0), osBlock.getChannelPointer (1), numSamples, midi);
        oversampler->processSamplesDown (block);
    }
    else
    {
        renderEngine (block.getChannelPointer (0), block.getChannelPointer (1), numSamples, midi);
    }

    float* L = work->getWritePointer (0);
    float* R = work->getWritePointer (1);

    // ---- effects, at base rate ------------------------------------------------
    const bool phaserOn = getBool (refs.phaserOn);
    const bool ensembleOn = getBool (refs.ensembleOn);
    const bool delayOn = getBool (refs.delayOn);
    const bool pumpOn = getBool (refs.pumpOn);

    const int phStages = juce::jlimit (2, 12, getInt (refs.phaserStages, 6));
    const float phRate = get (refs.phaserRate, 0.35f);
    const float phDepth = get (refs.phaserDepth, 0.7f);
    const float phCentre = get (refs.phaserCentre, 0.45f);
    const float phFb = get (refs.phaserFeedback, 0.6f);
    const float phSpread = get (refs.phaserSpread, 0.5f);
    const float phMix = get (refs.phaserMix, 0.5f);

    const float enRate = get (refs.ensembleRate, 0.6f);
    const float enDepth = get (refs.ensembleDepth, 0.6f);
    const float enMix = get (refs.ensembleMix, 0.5f);

    float dTimeL = get (refs.delayTime, 0.35f);
    if (getBool (refs.delaySync))
    {
        const int idx = juce::jlimit (0, ndp::kNumDivisions - 1, getInt (refs.delayDivision, 5));
        dTimeL = (float) ((double) ndp::kDivisions[idx].beats * 60.0 / juce::jmax (20.0, hostBpm));
    }
    const float dOffset = get (refs.delayOffset);
    const float dTimeR = juce::jlimit (0.01f, 3.9f, dTimeL * (1.0f + dOffset));
    const float dFb = get (refs.delayFeedback, 0.35f);
    const float dTone = get (refs.delayTone, 0.6f);
    const bool dPing = getBool (refs.delayPingPong);
    const float dMix = get (refs.delayMix, 0.25f);

    const float pDepth = get (refs.pumpDepth, 0.5f);
    const float pShape = get (refs.pumpShape, 0.5f);
    const int pumpIdx = juce::jlimit (0, ndp::kNumDivisions - 1, getInt (refs.pumpDivision, ndp::kDefaultDivision));
    const double pumpBeats = juce::jmax (0.01f, ndp::kDivisions[pumpIdx].beats);
    const double pumpInc = (hostBpm / 60.0) / pumpBeats / baseSampleRate;

    // When the transport is stopped the pump free-runs so the effect is still
    // audible while auditioning a patch.
    double phase = hostPlaying ? std::fmod (hostPpq / pumpBeats, 1.0) : pumpPhase;
    if (phase < 0.0)
        phase += 1.0;

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (get (refs.outputGain, -6.0f)));

    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = L[i], r = R[i];

        if (phaserOn)
            phaser.process (l, r, phStages, phRate, phDepth, phCentre, phFb, phSpread, phMix);

        if (ensembleOn)
            ensemble.process (l, r, enRate, enDepth, enMix);

        if (delayOn)
            delay.process (l, r, dTimeL, dTimeR, dFb, dTone, dPing, dMix);

        if (pumpOn)
        {
            const float g = nd::Pump::gainFor ((float) phase, pDepth, pShape);
            l *= g;
            r *= g;
        }

        phase += pumpInc;
        if (phase >= 1.0)
            phase -= 1.0;

        output.process (l, r, outputGain.getNextValue());

        L[i] = l;
        R[i] = r;
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
    }

    pumpPhase = phase;

    if (mono)
    {
        buffer.copyFrom (0, 0, *work, 0, 0, numSamples);
        buffer.addFrom (0, 0, *work, 1, 0, numSamples);
        buffer.applyGain (0.5f);
    }

    outputLevel.store (peak);
    activeVoices.store (engine.getActiveVoiceCount());

    midi.clear();
}

int NitedriveProcessor::getNumPrograms()
{
    return ndp::getNumPresets();
}

const juce::String NitedriveProcessor::getProgramName (int index)
{
    return ndp::getPresetName (index);
}

void NitedriveProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= ndp::getNumPresets())
        return;

    currentProgram = index;
    ndp::applyPreset (apvts, index);
    updateHostDisplay();
}

void NitedriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    ndp::writeState (apvts, currentProgram, destData);
}

void NitedriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    ndp::readState (apvts, data, sizeInBytes, currentProgram);
}

juce::AudioProcessorEditor* NitedriveProcessor::createEditor()
{
    return new NitedriveEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NitedriveProcessor();
}
