#include "PluginEditor.h"
#include "Presets.h"

using namespace ndg;

void LevelMeter::timerCallback()
{
    const float peak = proc.outputLevel.load();
    // Instant attack, gentle release: a meter that falls as fast as the audio is
    // unreadable on percussive patches.
    display = peak > display ? peak : display * 0.82f;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (r, 2.0f);

    const float db = juce::Decibels::gainToDecibels (juce::jmax (display, 1.0e-6f));
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);

    auto fill = r.reduced (1.5f);
    fill = fill.withWidth (fill.getWidth() * norm);

    g.setColour (display >= 0.99f ? Palette::meterHot : Palette::meter);
    g.fillRoundedRectangle (fill, 1.5f);

    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
}

NitedriveEditor::NitedriveEditor (NitedriveProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    for (int i = 0; i < ndp::getNumPresets(); ++i)
        presetBox.addItem (ndp::getPresetName (i), i + 1);

    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getCurrentProgram())
            proc.setCurrentProgram (idx);
    };

    prevButton.onClick = [this]
    {
        const int n = ndp::getNumPresets();
        proc.setCurrentProgram ((proc.getCurrentProgram() - 1 + n) % n);
        refreshPresetBox();
    };

    nextButton.onClick = [this]
    {
        const int n = ndp::getNumPresets();
        proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % n);
        refreshPresetBox();
    };

    content.addAndMakeVisible (presetBox);
    content.addAndMakeVisible (prevButton);
    content.addAndMakeVisible (nextButton);

    voiceLabel.setColour (juce::Label::textColourId, Palette::textDim);
    voiceLabel.setFont (juce::FontOptions (11.0f));
    voiceLabel.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (voiceLabel);

    meter = std::make_unique<LevelMeter> (proc);
    content.addAndMakeVisible (*meter);

    buildSections();
    refreshPresetBox();

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 2 / 3, kBaseHeight * 2 / 3, kBaseWidth * 3 / 2, kBaseHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);

    startTimerHz (8);
}

NitedriveEditor::~NitedriveEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void NitedriveEditor::buildSections()
{
    auto& state = proc.apvts;

    auto makeSection = [this] (const juce::String& title, int cols) -> Section&
    {
        auto s = std::make_unique<Section> (title, cols);
        auto& ref = *s;
        content.addAndMakeVisible (ref);
        sections.push_back (std::move (s));
        return ref;
    };

    auto addOsc = [&state] (Section& s, const juce::String& p, bool withSync)
    {
        s.add<Picker> (state, p + "Wave", "Wave");
        s.add<Knob>   (state, p + "Level", "Level");
        s.add<Knob>   (state, p + "Octave", "Octave");
        s.add<Knob>   (state, p + "Coarse", "Coarse");
        s.add<Knob>   (state, p + "Fine", "Fine");
        s.add<Knob>   (state, p + "PW", "Width");
        s.add<Knob>   (state, p + "Detune", "Detune");
        s.add<Knob>   (state, p + "Spread", "Spread");
        s.add<Knob>   (state, p + "Unison", "Unison");

        if (withSync)
            s.add<Toggle> (state, "osc2Sync", "SYNC");
    };

    addOsc (makeSection ("Oscillator 1", 5), "osc1", false);
    addOsc (makeSection ("Oscillator 2", 5), "osc2", true);

    {
        auto& s = makeSection ("Mixer", 3);
        s.add<Picker> (state, "subWave", "Sub Wave");
        s.add<Knob>   (state, "subOctave", "Sub Oct");
        s.add<Knob>   (state, "subLevel", "Sub");
        s.add<Knob>   (state, "noiseLevel", "Noise");
        s.add<Knob>   (state, "ringLevel", "Ring");
        s.add<Knob>   (state, "outputGain", "Output");
    }

    {
        auto& s = makeSection ("Filter", 4);
        s.add<Picker> (state, "filterMode", "Mode");
        s.add<Knob>   (state, "cutoff", "Cutoff");
        s.add<Knob>   (state, "resonance", "Reso");
        s.add<Knob>   (state, "filterDrive", "Drive");
        s.add<Knob>   (state, "filterEnv", "Env Amt");
        s.add<Knob>   (state, "keyTrack", "Key Trk");
        s.add<Knob>   (state, "velToCutoff", "Vel");
    }

    {
        auto& s = makeSection ("Amp Envelope", 5);
        s.add<Knob> (state, "ampA", "Attack");
        s.add<Knob> (state, "ampD", "Decay");
        s.add<Knob> (state, "ampS", "Sustain");
        s.add<Knob> (state, "ampR", "Release");
        s.add<Knob> (state, "velToAmp", "Vel");
    }

    {
        auto& s = makeSection ("Mod Envelope", 4);
        s.add<Knob> (state, "modA", "Attack");
        s.add<Knob> (state, "modD", "Decay");
        s.add<Knob> (state, "modS", "Sustain");
        s.add<Knob> (state, "modR", "Release");
    }

    {
        auto& s = makeSection ("Drive", 4);
        s.add<Picker> (state, "preDriveType", "Pre Type");
        s.add<Knob>   (state, "preDrive", "Pre");
        s.add<Picker> (state, "postDriveType", "Post Type");
        s.add<Knob>   (state, "postDrive", "Post");
    }

    auto addLfo = [&state] (Section& s, const juce::String& p)
    {
        s.add<Picker> (state, p + "Shape", "Shape");
        s.add<Knob>   (state, p + "Rate", "Rate");
        s.add<Picker> (state, p + "Division", "Division");
        s.add<Toggle> (state, p + "Sync", "SYNC");
        s.add<Toggle> (state, p + "Retrig", "RETRIG");
    };

    addLfo (makeSection ("LFO 1", 3), "lfo1");
    addLfo (makeSection ("LFO 2", 3), "lfo2");

    {
        auto& s = makeSection ("Voice", 4);
        s.add<Picker> (state, "voiceMode", "Mode");
        s.add<Knob>   (state, "glide", "Glide");
        s.add<Knob>   (state, "numVoices", "Voices");
        s.add<Knob>   (state, "bendRange", "Bend");
        s.add<Picker> (state, "quality", "Quality");
        s.add<Knob>   (state, "drift", "Drift");
        s.add<Toggle> (state, "randomPhase", "FREE PHASE");
        s.add<Toggle> (state, "glideLegato", "LEGATO GLIDE");
    }

    {
        auto& s = makeSection ("Modulation Matrix", 3);
        for (int i = 0; i < nd::kNumModSlots; ++i)
        {
            const auto n = juce::String (i + 1);
            s.add<Picker> (state, "modSrc" + n, i == 0 ? "Source" : "");
            s.add<Picker> (state, "modDst" + n, i == 0 ? "Destination" : "");
            s.add<Knob>   (state, "modAmt" + n, i == 0 ? "Amount" : "");
        }
    }

    {
        auto& s = makeSection ("Phaser", 4);
        s.add<Toggle> (state, "phaserOn", "ON");
        s.add<Knob>   (state, "phaserStages", "Stages");
        s.add<Knob>   (state, "phaserRate", "Rate");
        s.add<Knob>   (state, "phaserDepth", "Depth");
        s.add<Knob>   (state, "phaserCentre", "Centre");
        s.add<Knob>   (state, "phaserFeedback", "Fdbk");
        s.add<Knob>   (state, "phaserSpread", "Spread");
        s.add<Knob>   (state, "phaserMix", "Mix");
    }

    {
        auto& s = makeSection ("Ensemble", 2);
        s.add<Toggle> (state, "ensembleOn", "ON");
        s.add<Knob>   (state, "ensembleRate", "Rate");
        s.add<Knob>   (state, "ensembleDepth", "Depth");
        s.add<Knob>   (state, "ensembleMix", "Mix");
    }

    {
        auto& s = makeSection ("Delay", 5);
        s.add<Toggle> (state, "delayOn", "ON");
        s.add<Toggle> (state, "delaySync", "SYNC");
        s.add<Picker> (state, "delayDivision", "Division");
        s.add<Knob>   (state, "delayTime", "Time");
        s.add<Knob>   (state, "delayOffset", "Offset");
        s.add<Knob>   (state, "delayFeedback", "Fdbk");
        s.add<Knob>   (state, "delayTone", "Tone");
        s.add<Knob>   (state, "delayMix", "Mix");
        s.add<Toggle> (state, "delayPingPong", "PING PONG");
    }

    {
        auto& s = makeSection ("Pump", 2);
        s.add<Toggle> (state, "pumpOn", "ON");
        s.add<Picker> (state, "pumpDivision", "Division");
        s.add<Knob>   (state, "pumpDepth", "Depth");
        s.add<Knob>   (state, "pumpShape", "Shape");
    }
}

void NitedriveEditor::refreshPresetBox()
{
    const int program = proc.getCurrentProgram();
    presetBox.setSelectedId (program + 1, juce::dontSendNotification);
    lastSeenProgram = program;
}

void NitedriveEditor::timerCallback()
{
    if (proc.getCurrentProgram() != lastSeenProgram)
        refreshPresetBox();

    voiceLabel.setText (juce::String (proc.activeVoices.load()) + " voices",
                        juce::dontSendNotification);
}

void NitedriveEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    const float scale = juce::jmin ((float) getWidth()  / (float) kBaseWidth,
                                    (float) getHeight() / (float) kBaseHeight);
    g.addTransform (juce::AffineTransform::scale (scale));

    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (23.0f).withStyle ("Bold"));
    g.drawText ("NITEDRIVE", 14, 12, 190, 26, juce::Justification::centredLeft, false);

    g.setColour (Palette::accent);
    g.fillRect (14, 40, 172, 2);
}

void NitedriveEditor::resized()
{
    // Lay the content out at its design size, then scale the whole thing to fit.
    const float scale = juce::jmin ((float) getWidth()  / (float) kBaseWidth,
                                    (float) getHeight() / (float) kBaseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, kBaseWidth, kBaseHeight);

    auto r = juce::Rectangle<int> (0, 0, kBaseWidth, kBaseHeight).reduced (10);

    // ---- header ----
    auto header = r.removeFromTop (40);
    header.removeFromLeft (200);                       // logo, painted below
    prevButton.setBounds (header.removeFromLeft (28).reduced (2, 8));
    presetBox.setBounds (header.removeFromLeft (200).reduced (2, 8));
    nextButton.setBounds (header.removeFromLeft (28).reduced (2, 8));
    meter->setBounds (header.removeFromRight (180).reduced (4, 14));
    voiceLabel.setBounds (header.removeFromRight (90).reduced (2, 8));

    r.removeFromTop (8);

    auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
    auto col = [] (juce::Rectangle<int>& area, int w) { auto x = area.removeFromLeft (w); area.removeFromLeft (8); return x; };

    if (sections.size() < 15)
        return;

    auto a = row (152);
    sections[0]->setBounds (col (a, 380));   // Osc 1
    sections[1]->setBounds (col (a, 380));   // Osc 2
    sections[2]->setBounds (a);              // Mixer

    auto b = row (152);
    sections[3]->setBounds (col (b, 310));   // Filter
    sections[4]->setBounds (col (b, 300));   // Amp envelope
    sections[5]->setBounds (col (b, 250));   // Mod envelope
    sections[6]->setBounds (b);              // Drive

    auto c = row (152);
    sections[7]->setBounds (col (c, 220));   // LFO 1
    sections[8]->setBounds (col (c, 220));   // LFO 2
    sections[9]->setBounds (col (c, 330));   // Voice
    sections[14]->setBounds (c);             // Pump

    // The matrix needs six rows of pickers, so it gets the deepest row.
    auto d = row (r.getHeight());
    sections[10]->setBounds (col (d, 360));  // Mod matrix
    sections[11]->setBounds (col (d, 280));  // Phaser
    sections[12]->setBounds (col (d, 160));  // Ensemble
    sections[13]->setBounds (d);             // Delay
}
