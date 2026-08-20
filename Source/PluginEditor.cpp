#include "PluginEditor.h"
#include "Presets.h"
#include "Version.h"

using namespace ndg;

void LevelMeter::timerCallback()
{
    const float peak = proc.outputLevel.load();
    // Instant attack, gentle release: a meter that falls as fast as the audio is
    // unreadable on percussive patches.
    display = peak > display ? peak : display * 0.82f;

    // Latch the hot colour for about a second so a single clipped transient is
    // still visible after the level has fallen away.
    if (display >= 0.99f)
        clipHoldFrames = 30;
    else if (clipHoldFrames > 0)
        --clipHoldFrames;

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

    g.setColour (clipHoldFrames > 0 ? Palette::meterHot : Palette::meter);
    g.fillRoundedRectangle (fill, 1.5f);

    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("OUT", getLocalBounds().reduced (4, 0),
                juce::Justification::centredLeft, false);
}

NitedriveEditor::NitedriveEditor (NitedriveProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    rescanUserPresets();
    rebuildPresetBoxItems();

    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();

        if (id >= kUserPresetIdBase)
        {
            loadUserPreset (id - kUserPresetIdBase);
        }
        else if (id >= 1)
        {
            // Applied even when the id matches the current program: reselecting a
            // preset marked dirty ('*') is how you revert your edits.
            proc.setCurrentProgram (id - 1);
            lastSeenProgram = id - 1;
            presetLoaded (ndp::getPresetName (id - 1), id);
        }
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

    saveButton.onClick = [this] { promptSaveUserPreset(); };

    content.addAndMakeVisible (presetBox);
    content.addAndMakeVisible (prevButton);
    content.addAndMakeVisible (nextButton);
    content.addAndMakeVisible (saveButton);

    versionLabel.setText (ND_BUILD_STRING, juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, Palette::textDim);
    versionLabel.setFont (juce::FontOptions (10.0f));
    versionLabel.setJustificationType (juce::Justification::centredRight);
    versionLabel.setTooltip ("NITEDRIVE " ND_VERSION_STRING ", built from commit " ND_GIT_SHA
                             " (an asterisk means uncommitted changes were present)");
    content.addAndMakeVisible (versionLabel);

    voiceLabel.setColour (juce::Label::textColourId, Palette::textDim);
    voiceLabel.setFont (juce::FontOptions (11.0f));
    voiceLabel.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (voiceLabel);

    meter = std::make_unique<LevelMeter> (proc);
    content.addAndMakeVisible (*meter);

    buildSections();
    refreshPresetBox();

    // The listener survives replaceState(): JUCE keeps listeners attached to the
    // wrapper object when the underlying tree is swapped out.
    proc.apvts.state.addListener (this);

    for (auto* id : { "delaySync", "lfo1Sync", "lfo2Sync" })
        proc.apvts.addParameterListener (id, this);

    updateSyncDependentControls();

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 4 / 5, kBaseHeight * 4 / 5, kBaseWidth * 3 / 2, kBaseHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);

    startTimerHz (8);
}

NitedriveEditor::~NitedriveEditor()
{
    for (auto* id : { "delaySync", "lfo1Sync", "lfo2Sync" })
        proc.apvts.removeParameterListener (id, this);

    cancelPendingUpdate();
    proc.apvts.state.removeListener (this);

    // A save dialog left open when the host tears the editor down would outlive the
    // LookAndFeel it points at and fire its callback into a dead editor.
    if (saveDialog != nullptr)
    {
        saveDialog->setLookAndFeel (nullptr);
        saveDialog->exitModalState (0);
        delete saveDialog.getComponent();
    }

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
        s.add<Knob>   (state, p + "PW", "PW");
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
        s.add<Knob>   (state, "subOctave", "Sub Oct", Knob::Style::incDec);
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
        // Two columns, two rows: type beside its amount, and the pickers get
        // enough width to show the full type names.
        auto& s = makeSection ("Drive", 2);
        s.add<Picker> (state, "preDriveType", "Pre Type");
        s.add<Knob>   (state, "preDrive", "Pre");
        s.add<Picker> (state, "postDriveType", "Post Type");
        s.add<Knob>   (state, "postDrive", "Post");
    }

    auto addLfo = [&state] (Section& s, const juce::String& p,
                            Knob*& rateOut, Picker*& divisionOut)
    {
        s.add<Picker> (state, p + "Shape", "Shape");
        rateOut     = &s.add<Knob>   (state, p + "Rate", "Rate");
        divisionOut = &s.add<Picker> (state, p + "Division", "Division");
        s.add<Toggle> (state, p + "Sync", "SYNC");
        s.add<Toggle> (state, p + "Retrig", "RETRIG");
    };

    addLfo (makeSection ("LFO 1", 3), "lfo1", lfo1RateKnob, lfo1DivisionPicker);
    addLfo (makeSection ("LFO 2", 3), "lfo2", lfo2RateKnob, lfo2DivisionPicker);

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
        s.setColumnHeaders ({ "Source", "Destination", "Amount" });

        for (int i = 0; i < nd::kNumModSlots; ++i)
        {
            const auto n = juce::String (i + 1);
            s.add<Picker> (state, "modSrc" + n, "");
            s.add<Picker> (state, "modDst" + n, "");
            s.add<Knob>   (state, "modAmt" + n, "");
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
        delayDivisionPicker = &s.add<Picker> (state, "delayDivision", "Division");
        delayTimeKnob       = &s.add<Knob>   (state, "delayTime", "Time");
        s.add<Knob>   (state, "delayOffset", "Offset");
        s.add<Knob>   (state, "delayFeedback", "Fdbk");
        s.add<Knob>   (state, "delayTone", "Tone");
        s.add<Knob>   (state, "delayMix", "Mix");
        s.add<Toggle> (state, "delayPingPong", "P-PONG");   // "PING PONG" clips at this cell width
    }

    {
        auto& s = makeSection ("Pump", 2);
        s.add<Toggle> (state, "pumpOn", "ON");
        s.add<Picker> (state, "pumpDivision", "Division");
        s.add<Knob>   (state, "pumpDepth", "Depth");
        s.add<Knob>   (state, "pumpShape", "Shape");
    }
}

// ---------------------------------------------------------------------------
// Presets

juce::File NitedriveEditor::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("NITEDRIVE")
               .getChildFile ("Presets");
}

void NitedriveEditor::rescanUserPresets()
{
    userPresetFiles = userPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.xml");

    std::sort (userPresetFiles.begin(), userPresetFiles.end(),
               [] (const juce::File& a, const juce::File& b)
               {
                   return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
               });
}

void NitedriveEditor::rebuildPresetBoxItems()
{
    presetBox.clear (juce::dontSendNotification);

    for (int i = 0; i < ndp::getNumPresets(); ++i)
        presetBox.addItem (ndp::getPresetName (i), i + 1);

    if (! userPresetFiles.isEmpty())
    {
        presetBox.addSectionHeading (juce::String::fromUTF8 ("\xe2\x80\x94 User \xe2\x80\x94"));

        for (int i = 0; i < userPresetFiles.size(); ++i)
            presetBox.addItem (userPresetFiles.getReference (i).getFileNameWithoutExtension(),
                               kUserPresetIdBase + i);
    }
}

void NitedriveEditor::promptSaveUserPreset()
{
    auto* window = new juce::AlertWindow ("Save Preset",
                                          "Save the current settings as a user preset.",
                                          juce::MessageBoxIconType::NoIcon,
                                          this);
    // An AlertWindow is a parentless top-level component: it does NOT inherit the
    // editor's LookAndFeel, so without this it renders in stock JUCE grey and the
    // dialog colours defined in LookAndFeel.h are dead code.
    window->setLookAndFeel (&lnf);
    saveDialog = window;
    window->addTextEditor ("name", currentPresetName, "Name:");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<NitedriveEditor> safeThis (this);

    window->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, window] (int result)
        {
            if (safeThis != nullptr && result == 1)
                safeThis->saveUserPreset (window->getTextEditorContents ("name"));
        }),
        true);   // delete the window when dismissed

    // The alert window does not hand focus to its editor on its own; without this
    // the user has to click the field before they can type.
    if (auto* editor = window->getTextEditor ("name"))
    {
        editor->setSelectAllWhenFocused (true);
        editor->grabKeyboardFocus();
    }
}

void NitedriveEditor::showSaveError (const juce::String& path)
{
    // A silently discarded save reads as a saved preset that later vanished.
    auto opts = juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle ("Preset not saved")
                    .withMessage ("Could not write:\n" + path)
                    .withButton ("OK")
                    .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (opts, nullptr);
}

void NitedriveEditor::saveUserPreset (const juce::String& rawName)
{
    const auto legal = juce::File::createLegalFileName (rawName.trim());

    if (legal.isEmpty())
        return;   // nothing sensible to call it; quietly drop the save

    auto dir = userPresetDirectory();

    if (! dir.createDirectory())
    {
        showSaveError (dir.getFullPathName());
        return;
    }

    // Same name = overwrite: saving over your own preset is the common case.
    auto file = dir.getChildFile (legal + ".xml");

    if (auto xml = proc.apvts.copyState().createXml())
    {
        if (! xml->writeTo (file))
        {
            showSaveError (file.getFullPathName());
            return;
        }
    }
    else
    {
        showSaveError (file.getFullPathName());
    }

    rescanUserPresets();
    rebuildPresetBoxItems();

    const int index = userPresetFiles.indexOf (file);

    lastSeenProgram = proc.getCurrentProgram();
    presetLoaded (file.getFileNameWithoutExtension(),
                  index >= 0 ? kUserPresetIdBase + index : 0);
}

void NitedriveEditor::loadUserPreset (int userIndex)
{
    if (! juce::isPositiveAndBelow (userIndex, userPresetFiles.size()))
        return;

    const auto file = userPresetFiles.getReference (userIndex);
    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName (proc.apvts.state.getType()))
    {
        // Unreadable or foreign file: put the selection back on solid ground.
        refreshPresetBox();
        return;
    }

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
    {
        refreshPresetBox();
        return;
    }

    proc.apvts.replaceState (tree);

    // The host program has not changed; stop the timer from stamping the factory
    // name back over the user selection.
    lastSeenProgram = proc.getCurrentProgram();
    presetLoaded (file.getFileNameWithoutExtension(), kUserPresetIdBase + userIndex);
}

void NitedriveEditor::presetLoaded (const juce::String& name, int comboId)
{
    currentPresetName = name;
    currentPresetId = comboId;

    // Snapshot the freshly loaded values; the '*' marker compares against these.
    // Read from the parameters, not the tree: parameter writes land synchronously,
    // the tree flush can lag a timer tick behind.
    referenceValues.clear();

    for (auto* param : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            referenceValues.emplace_back (ranged, ranged->getValue());

    stateDirtyPending = false;

    if (shownDirty)
        shownDirty = false;

    // Selection travels by item id, never by text: ComboBox::setText re-selects the
    // first item whose text matches, so a user preset named like a factory preset
    // would hijack the selection and leave the factory patch unloadable.
    if (currentPresetId > 0)
        presetBox.setSelectedId (currentPresetId, juce::dontSendNotification);
    else
        presetBox.setText (currentPresetName, juce::dontSendNotification);

    updateSyncDependentControls();
}

void NitedriveEditor::updateDirtyIndicator()
{
    bool dirty = false;

    for (const auto& [param, reference] : referenceValues)
    {
        if (std::abs (param->getValue() - reference) > 1.0e-4f)
        {
            dirty = true;
            break;
        }
    }

    if (dirty != shownDirty)
    {
        shownDirty = dirty;

        // The starred text matches no item so it cannot mis-select; the clean state
        // goes back to the id for the same reason as in presetLoaded().
        if (dirty || currentPresetId <= 0)
            presetBox.setText (dirty ? "* " + currentPresetName : currentPresetName,
                               juce::dontSendNotification);
        else
            presetBox.setSelectedId (currentPresetId, juce::dontSendNotification);
    }
}

void NitedriveEditor::refreshPresetBox()
{
    const int program = proc.getCurrentProgram();
    lastSeenProgram = program;
    presetLoaded (ndp::getPresetName (program), program + 1);
}

// ---------------------------------------------------------------------------
// Sync toggles vs. the controls they supersede

void NitedriveEditor::parameterChanged (const juce::String&, float)
{
    // Parameter listeners may fire from any thread; hop to the message thread
    // before touching components. AsyncUpdater is safe to trigger from anywhere
    // and is cancelled in the destructor, so no callback can outlive the editor.
    triggerAsyncUpdate();
}

void NitedriveEditor::handleAsyncUpdate()
{
    updateSyncDependentControls();
}

void NitedriveEditor::updateSyncDependentControls()
{
    auto syncOn = [this] (const char* id)
    {
        if (auto* value = proc.apvts.getRawParameterValue (id))
            return value->load() >= 0.5f;

        return false;
    };

    auto apply = [] (ndg::Knob* timeKnob, ndg::Picker* divisionPicker, bool sync)
    {
        // Free-running time/rate applies when sync is off; the division only
        // matters when sync is on.
        if (timeKnob != nullptr)
            timeKnob->setControlEnabled (! sync);

        if (divisionPicker != nullptr)
            divisionPicker->setControlEnabled (sync);
    };

    apply (delayTimeKnob, delayDivisionPicker, syncOn ("delaySync"));
    apply (lfo1RateKnob,  lfo1DivisionPicker,  syncOn ("lfo1Sync"));
    apply (lfo2RateKnob,  lfo2DivisionPicker,  syncOn ("lfo2Sync"));
}

// ---------------------------------------------------------------------------

void NitedriveEditor::timerCallback()
{
    if (proc.getCurrentProgram() != lastSeenProgram)
        refreshPresetBox();

    if (stateDirtyPending)
    {
        stateDirtyPending = false;
        updateDirtyIndicator();
    }

    voiceLabel.setText ("VOICES " + juce::String (proc.activeVoices.load()),
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
    saveButton.setBounds (header.removeFromLeft (56).reduced (2, 8));
    meter->setBounds (header.removeFromRight (180).reduced (4, 14));
    voiceLabel.setBounds (header.removeFromRight (90).reduced (2, 8));
    versionLabel.setBounds (header.removeFromRight (130).reduced (2, 8));

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
    sections[9]->setBounds (col (c, 370));   // Voice - wide enough for "LEGATO GLIDE"
    sections[14]->setBounds (c);             // Pump

    // The matrix needs six rows of pickers, so it gets the deepest row.
    auto d = row (r.getHeight());
    sections[10]->setBounds (col (d, 360));  // Mod matrix
    sections[11]->setBounds (col (d, 270));  // Phaser
    sections[12]->setBounds (col (d, 150));  // Ensemble
    sections[13]->setBounds (d);             // Delay
}
