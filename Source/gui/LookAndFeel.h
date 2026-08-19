#pragma once

#include <JuceHeader.h>

namespace ndg
{

/** Palette. Deliberately stark - near-black panels, bone-white text, one hot accent
    for anything that carries a value. */
namespace Palette
{
    const juce::Colour background   { 0xff0d0d0f };
    const juce::Colour panel        { 0xff17171b };
    const juce::Colour panelEdge    { 0xff2a2a31 };
    const juce::Colour text         { 0xffe8e6e1 };
    const juce::Colour textDim      { 0xff8b8892 };
    const juce::Colour accent       { 0xffff3b26 };
    const juce::Colour accentDim    { 0xff5c1a12 };
    const juce::Colour track        { 0xff34343d };
    const juce::Colour meter        { 0xff4fd67a };
    const juce::Colour meterHot     { 0xffff3b26 };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
        setColour (juce::Label::textColourId,                 Palette::text);
        setColour (juce::ComboBox::backgroundColourId,        Palette::panel);
        setColour (juce::ComboBox::textColourId,              Palette::text);
        setColour (juce::ComboBox::outlineColourId,           Palette::panelEdge);
        setColour (juce::ComboBox::arrowColourId,             Palette::textDim);
        setColour (juce::PopupMenu::backgroundColourId,       Palette::panel);
        setColour (juce::PopupMenu::textColourId,             Palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accentDim);
        setColour (juce::PopupMenu::highlightedTextColourId,  Palette::text);
        setColour (juce::TextButton::buttonColourId,          Palette::panel);
        setColour (juce::TextButton::buttonOnColourId,        Palette::accentDim);
        setColour (juce::TextButton::textColourOffId,         Palette::textDim);
        setColour (juce::TextButton::textColourOnId,          Palette::text);
        setColour (juce::ToggleButton::textColourId,          Palette::text);
        setColour (juce::ToggleButton::tickColourId,          Palette::accent);
        setColour (juce::Slider::textBoxTextColourId,         Palette::text);
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto thickness = juce::jmax (2.5f, radius * 0.16f);
        const auto arcRadius = radius - thickness * 0.5f;
        const auto angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (Palette::track);
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Bipolar controls fill outward from twelve o'clock rather than from the left,
        // so "no modulation" reads as empty instead of half full.
        const bool bipolar = slider.getMinimum() < -0.0001 && slider.getMaximum() > 0.0001;
        const float originAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        if (std::abs (angle - originAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (originAngle, angle),
                                 juce::jmax (originAngle, angle), true);
            g.setColour (slider.isEnabled() ? Palette::accent : Palette::track);
            g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        juce::Path pointer;
        const float pointerLength = arcRadius * 0.62f;
        pointer.startNewSubPath (centre.x, centre.y - arcRadius * 0.24f);
        pointer.lineTo (centre.x, centre.y - pointerLength - arcRadius * 0.24f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));

        g.setColour (Palette::text);
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.6f, thickness * 0.45f),
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool highlighted, bool /*down*/) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();

        g.setColour (on ? Palette::accentDim : Palette::panel);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (on ? Palette::accent : Palette::panelEdge);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (bounds, 3.0f);
        }

        g.setColour (on ? Palette::text : Palette::textDim);
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawText (b.getButtonText(), bounds, juce::Justification::centred, false);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (11.5f));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (juce::FontOptions (13.0f));
    }
};

} // namespace ndg
