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
};

class NitedriveEditor : public juce::AudioProcessorEditor, private juce::Timer
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

    static constexpr int kBaseWidth = 1140;
    static constexpr int kBaseHeight = 790;

    NitedriveProcessor& proc;
    ndg::LookAndFeel lnf;

    /** Everything lives on this fixed-size child; the editor scales it to fit,
        which keeps the layout code in real pixels and still resizes cleanly. */
    juce::Component content;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::Label voiceLabel;
    std::unique_ptr<LevelMeter> meter;

    std::vector<std::unique_ptr<ndg::Section>> sections;
    int lastSeenProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NitedriveEditor)
};
