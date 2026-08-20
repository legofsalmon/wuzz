#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/LookAndFeel.h"

/** Output meter with a slow decay, so short stabs stay readable. */
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter (NitedriveProcessor& p) : proc (p) { startTimerHz (30); }
    ~LevelMeter() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    NitedriveProcessor& proc;
    float display = 0.0f;
    int clipHoldFrames = 0;   // frames left holding the hot colour after a clip
};

class NitedriveEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::ValueTree::Listener,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    explicit NitedriveEditor (NitedriveProcessor&);
    ~NitedriveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buildSections();
    void timerCallback() override;
    void refreshPresetBox();

    // ---- user presets (all editor-side; the processor knows nothing about them) ----
    static juce::File userPresetDirectory();
    void rescanUserPresets();
    void rebuildPresetBoxItems();
    void promptSaveUserPreset();
    void saveUserPreset (const juce::String& rawName);
    void loadUserPreset (int userIndex);

    /** Called after any preset load; remembers the clean state for the '*' marker. */
    void presetLoaded (const juce::String& name, int comboId);
    void showSaveError (const juce::String& path);
    void updateDirtyIndicator();

    // ---- sync toggles greying out the controls they supersede ----
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void updateSyncDependentControls();

    // ---- dirty tracking: a single tree listener sets a flag, the 8Hz timer compares ----
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { stateDirtyPending = true; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { stateDirtyPending = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { stateDirtyPending = true; }
    void valueTreeRedirected (juce::ValueTree&) override                               { stateDirtyPending = true; }

    static constexpr int kBaseWidth = 1140;
    static constexpr int kBaseHeight = 790;
    static constexpr int kUserPresetIdBase = 1000;   // combo ids >= this are user presets

    NitedriveProcessor& proc;
    ndg::LookAndFeel lnf;

    /** Everything lives on this fixed-size child; the editor scales it to fit,
        which keeps the layout code in real pixels and still resizes cleanly. */
    juce::Component content;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    juce::Label voiceLabel;
    std::unique_ptr<LevelMeter> meter;

    std::vector<std::unique_ptr<ndg::Section>> sections;
    int lastSeenProgram = -1;

    // Controls that a sync toggle supersedes; owned by their sections.
    ndg::Knob*   delayTimeKnob = nullptr;
    ndg::Picker* delayDivisionPicker = nullptr;
    ndg::Knob*   lfo1RateKnob = nullptr;
    ndg::Picker* lfo1DivisionPicker = nullptr;
    ndg::Knob*   lfo2RateKnob = nullptr;
    ndg::Picker* lfo2DivisionPicker = nullptr;

    juce::Array<juce::File> userPresetFiles;
    juce::String currentPresetName;
    int currentPresetId = 0;
    juce::Component::SafePointer<juce::AlertWindow> saveDialog;
    bool stateDirtyPending = false;
    bool shownDirty = false;
    std::vector<std::pair<juce::RangedAudioParameter*, float>> referenceValues;

    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NitedriveEditor)
};
