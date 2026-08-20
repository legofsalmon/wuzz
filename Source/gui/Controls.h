#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"

namespace ndg
{

using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary control with its caption underneath and the value shown below.

    In cells too short for a rotary plus a text box (the modulation matrix rows) it
    falls back to a horizontal bar with the value in a popup while dragging. Small
    two-state integers can ask for inc/dec buttons instead of a dial. */
class Knob : public juce::Component
{
public:
    enum class Style { rotary, incDec };

    Knob (APVTS& state, const juce::String& paramId, const juce::String& caption,
          Style styleToUse = Style::rotary)
        : style (styleToUse)
    {
        if (style == Style::incDec)
        {
            slider.setSliderStyle (juce::Slider::IncDecButtons);
            slider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
        }
        else
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        }

        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 14);

        // Shift-drag switches to fine adjustment.
        slider.setVelocityBasedMode (false);
        slider.setVelocityModeParameters (1.0, 1, 0.0, true, juce::ModifierKeys::shiftModifier);

        auto* param = state.getParameter (paramId);
        jassert (param != nullptr);

        if (param != nullptr)
        {
            slider.setTooltip (param->getName (64));

            // Small integer ranges feel dead when a full drag is 250px; make the
            // whole travel about 120px so each step is a deliberate flick.
            if (auto* intParam = dynamic_cast<juce::AudioParameterInt*> (param))
                if (intParam->getRange().getLength() <= 24)
                    slider.setMouseDragSensitivity (120);
        }

        addAndMakeVisible (slider);

        if (caption.isNotEmpty())
        {
            label.setText (caption, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, Palette::textDim);
            label.setFont (juce::FontOptions (10.5f));
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
        }

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);

        // After attaching, the slider's range is the parameter's real range, so the
        // default can be expressed in real units.
        if (param != nullptr)
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    void setControlEnabled (bool shouldBeEnabled)
    {
        setEnabled (shouldBeEnabled);
        setAlpha (shouldBeEnabled ? 1.0f : 0.45f);
    }

    void resized() override
    {
        auto r = getLocalBounds();

        if (label.getText().isNotEmpty())
            label.setBounds (r.removeFromBottom (13));

        const bool compact = style == Style::rotary && r.getHeight() < 48;

        if (compact != isCompact)
        {
            isCompact = compact;

            if (compact)
            {
                slider.setSliderStyle (juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setPopupDisplayEnabled (true, true,
                                               findParentComponentOfClass<juce::AudioProcessorEditor>());
            }
            else
            {
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 14);
                slider.setPopupDisplayEnabled (false, false, nullptr);
            }
        }

        slider.setBounds (r);
    }

private:
    Style style;
    bool isCompact = false;
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

        // The attachment maps values to item indices but does NOT create the items -
        // an unpopulated ComboBox renders as a working-looking, permanently empty
        // menu. This shipped: every dropdown in the plugin was unselectable.
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (choice->choices, 1);
        else
            jassertfalse;   // Picker is only meant for choice parameters

        if (auto* param = state.getParameter (paramId))
            box.setTooltip (param->getName (64));

        addAndMakeVisible (box);

        if (caption.isNotEmpty())
        {
            label.setText (caption, juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, Palette::textDim);
            label.setFont (juce::FontOptions (10.5f));
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
        }

        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
    }

    void setControlEnabled (bool shouldBeEnabled)
    {
        setEnabled (shouldBeEnabled);
        setAlpha (shouldBeEnabled ? 1.0f : 0.45f);
    }

    void resized() override
    {
        auto r = getLocalBounds();

        if (label.getText().isNotEmpty())
            label.setBounds (r.removeFromTop (13));

        // Never let the box balloon to fill a tall cell.
        box.setBounds (r.removeFromTop (juce::jmin (26, r.getHeight())).reduced (0, 2));
    }

private:
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

/** Latching button bound to a bool parameter. Draws at a fixed compact size in the
    middle of whatever cell it is given, so grid cells of different shapes still
    produce uniform buttons. */
class Toggle : public juce::Component
{
public:
    Toggle (APVTS& state, const juce::String& paramId, const juce::String& caption)
    {
        button.setButtonText (caption);
        button.setClickingTogglesState (true);

        if (auto* param = state.getParameter (paramId))
            button.setTooltip (param->getName (64));

        addAndMakeVisible (button);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, button);
    }

    void resized() override
    {
        button.setBounds (getLocalBounds().withSizeKeepingCentre (
            juce::jmin (getWidth() - 8, 90), 24));
    }

private:
    juce::ToggleButton button;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** A titled panel that lays its children out on a fixed grid.

    Children are added left to right, wrapping into new rows. Keeping the layout here
    rather than in the editor means adding a control to a section is one line.
    Optional column headers are painted once above the grid, so table-like sections
    (the modulation matrix) don't repeat captions on every row. */
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

    void setColumnHeaders (juce::StringArray headers)
    {
        columnHeaders = std::move (headers);
        headerHeight = columnHeaders.isEmpty() ? 0 : 14;
        resized();
        repaint();
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

        if (! columnHeaders.isEmpty())
        {
            auto strip = getLocalBounds().reduced (6);
            strip.removeFromTop (16);
            strip = strip.removeFromTop (headerHeight);

            const int cellW = strip.getWidth() / juce::jmax (1, numColumns);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.5f));

            for (int c = 0; c < juce::jmin (numColumns, columnHeaders.size()); ++c)
                g.drawText (columnHeaders[c],
                            strip.getX() + c * cellW, strip.getY(), cellW, strip.getHeight(),
                            juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (16 + headerHeight);

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
    int headerHeight = 0;
    juce::StringArray columnHeaders;
    std::vector<std::unique_ptr<juce::Component>> items;
};

} // namespace ndg
