#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"

namespace ndg
{

using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary control with its caption underneath and the value shown on hover/drag. */
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 14);
        slider.setColour (juce::Slider::textBoxTextColourId, Palette::textDim);
        addAndMakeVisible (slider);

        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Palette::textDim);
        label.setFont (juce::FontOptions (10.5f));
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromBottom (13));
        slider.setBounds (r);
    }

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

/** Combo box with a caption above it. */
class Picker : public juce::Component
{
public:
    Picker (APVTS& state, const juce::String& paramId, const juce::String& caption)
    {
        box.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (box);

        label.setText (caption, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, Palette::textDim);
        label.setFont (juce::FontOptions (10.5f));
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (13));
        box.setBounds (r.reduced (0, 2));
    }

private:
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

/** Latching button bound to a bool parameter. */
class Toggle : public juce::Component
{
public:
    Toggle (APVTS& state, const juce::String& paramId, const juce::String& caption)
    {
        button.setButtonText (caption);
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

private:
    juce::ToggleButton button;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** A titled panel that lays its children out on a fixed grid.

    Children are added left to right, wrapping into new rows. Keeping the layout here
    rather than in the editor means adding a control to a section is one line. */
class Section : public juce::Component
{
public:
    Section (juce::String sectionTitle, int columns)
        : title (std::move (sectionTitle)), numColumns (columns) {}

    template <typename T, typename... Args>
    T& add (Args&&... args)
    {
        auto owned = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *owned;
        addAndMakeVisible (ref);
        items.push_back (std::move (owned));
        return ref;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Palette::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        g.setColour (Palette::accent);
        g.fillRect (juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 9.0f, 14.0f, 2.0f));

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
        g.drawText (title.toUpperCase(),
                    getLocalBounds().withTrimmedLeft (28).withHeight (20),
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (16);

        if (items.empty())
            return;

        const int rows = (int) std::ceil ((double) items.size() / (double) numColumns);
        const int cellW = r.getWidth() / numColumns;
        const int cellH = r.getHeight() / juce::jmax (1, rows);

        for (size_t i = 0; i < items.size(); ++i)
        {
            const int col = (int) i % numColumns;
            const int row = (int) i / numColumns;
            items[i]->setBounds (r.getX() + col * cellW,
                                 r.getY() + row * cellH,
                                 cellW, cellH);
        }
    }

private:
    juce::String title;
    int numColumns;
    std::vector<std::unique_ptr<juce::Component>> items;
};

} // namespace ndg
