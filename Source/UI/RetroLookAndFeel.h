#pragma once
#include <JuceHeader.h>
#include "Hardware3DKit.h"

class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    enum { primaryLed = 0x2400001, secondaryLed, tertiaryLed };

    static juce::String paletteName (int index)
    {
        return juce::StringArray { "MINT", "AMBER", "ICE", "VIOLET" }[juce::jlimit (0, 3, index)];
    }

    RetroLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff080b0d));
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe0e7e3));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff080d0f));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff35464a));
        setColour (juce::Slider::trackColourId, juce::Colour (0xff27383d));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xff76ffe0));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d2629));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff203632));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0c97e));
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xffeafff7));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0a1012));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff43565b));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xffdce5df));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff76ffe0));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0a1012));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffdde5de));
        setColour (juce::ToggleButton::textColourId, juce::Colour (0xffc8d3cf));
        setPalette (0);
    }

    void setPalette (int index)
    {
        const juce::uint32 primary[] { 0xff76ffe0, 0xffffbd65, 0xff73d8ff, 0xffc9a0ff };
        const juce::uint32 secondary[] { 0xfff3c077, 0xffa6e08b, 0xffff8da9, 0xff78f1e2 };
        const juce::uint32 tertiary[] { 0xff75cfff, 0xffffe5aa, 0xff96afff, 0xffff91b8 };

        paletteIndex = juce::jlimit (0, 3, index);
        setColour (primaryLed, juce::Colour (primary[paletteIndex]));
        setColour (secondaryLed, juce::Colour (secondary[paletteIndex]));
        setColour (tertiaryLed, juce::Colour (tertiary[paletteIndex]));
        setColour (juce::Slider::thumbColourId, findColour (primaryLed));
        setColour (juce::TextButton::textColourOnId, findColour (primaryLed));
        setColour (juce::TextButton::textColourOffId, findColour (secondaryLed));
        setColour (juce::ComboBox::arrowColourId, findColour (primaryLed));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, findColour (primaryLed).withAlpha (0.18f));
    }

    int getTabButtonBestWidth (juce::TabBarButton& button, int) override
    {
        return std::max (52, button.getButtonText().length() * 7 + 24);
    }

    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool over, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (1.0f, 2.0f);
        RetroHardware3D::drawRaisedButton (g, r, palette(), button.getToggleState(), over, down, button.isEnabled());

        if (button.getToggleState())
        {
            g.setColour (findColour (primaryLed));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - 3.0f).reduced (7.0f, 0.0f), 1.5f);
        }

        drawTabButtonText (button, g, over, down);
    }

    void drawTabButtonText (juce::TabBarButton& button, juce::Graphics& g, bool over, bool down) override
    {
        auto colour = button.getToggleState() ? findColour (primaryLed) : juce::Colour (0xffc3d0cd);
        if (over || down) colour = colour.brighter (0.08f);
        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawFittedText (button.getButtonText(), button.getActiveArea().reduced (5, 1),
                          juce::Justification::centred, 1);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& slider) override
    {
        auto available = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        RetroHardware3D::drawRotaryKnob (g, available, pos, start, end, palette(),
                                         slider.isMouseOverOrDragging());
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        RetroHardware3D::drawRaisedButton (g, button.getLocalBounds().toFloat().reduced (0.5f),
                                           palette(), button.getToggleState(), highlighted, down,
                                           button.isEnabled());
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool highlighted, bool down) override
    {
        auto font = getTextButtonFont (button, button.getHeight());
        font.setHeight (juce::jlimit (9.0f, 13.0f, (float) button.getHeight() * 0.38f));
        font.setBold (true);
        g.setFont (font);

        auto textColour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                     : juce::TextButton::textColourOffId);
        if (! button.isEnabled()) textColour = textColour.withMultipliedAlpha (0.40f);
        else if (highlighted || down) textColour = textColour.brighter (0.10f);

        g.setColour (textColour);
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (7, 3),
                          juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        RetroHardware3D::drawRaisedButton (g, r, palette(), false, box.isMouseOver(), isButtonDown,
                                           box.isEnabled());

        auto arrow = juce::Rectangle<float> ((float) buttonX, (float) buttonY,
                                             (float) buttonW, (float) buttonH)
                         .reduced ((float) buttonW * 0.28f, (float) buttonH * 0.34f);
        juce::Path path;
        path.startNewSubPath (arrow.getX(), arrow.getY());
        path.lineTo (arrow.getCentreX(), arrow.getBottom());
        path.lineTo (arrow.getRight(), arrow.getY());
        path.closeSubPath();

        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (path);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
        g.fillAll (juce::Colour (0xff070b0d));
        RetroHardware3D::drawRecessedPanel (g, r, palette(), 7.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos,
                                                    minSliderPos, maxSliderPos, style, slider);
            return;
        }

        RetroHardware3D::drawLinearTrack (g,
                                          juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h),
                                          sliderPos, palette(), slider.isMouseOverOrDragging());
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const float rockerWidth = juce::jlimit (36.0f, 64.0f, bounds.getHeight() * 1.9f);
        auto rocker = bounds.removeFromLeft (rockerWidth).reduced (1.0f, 3.0f);

        RetroHardware3D::drawRocker (g, rocker, palette(), button.getToggleState(), highlighted, down);

        auto textArea = button.getLocalBounds().withTrimmedLeft ((int) rockerWidth + 5).reduced (2, 0);
        auto colour = button.findColour (juce::ToggleButton::textColourId);
        if (! button.isEnabled()) colour = colour.withMultipliedAlpha (0.40f);
        else if (highlighted || down) colour = colour.brighter (0.08f);

        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (9.0f, 12.0f, bounds.getHeight() * 0.34f),
                                                   juce::Font::bold)));
        g.drawFittedText (button.getButtonText(), textArea, juce::Justification::centredLeft, 1);
    }

    void drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int width, int height,
                          double progress, const juce::String& textToShow) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
        RetroHardware3D::drawRecessedPanel (g, r, palette(), 4.0f);

        if (progress >= 0.0)
        {
            auto inner = r.reduced (4.0f);
            auto fill = inner.withWidth (inner.getWidth() * (float) juce::jlimit (0.0, 1.0, progress));
            g.setColour (findColour (primaryLed).withAlpha (0.20f));
            g.fillRoundedRectangle (fill, 2.0f);
            g.setColour (findColour (primaryLed));
            g.fillRoundedRectangle (fill.reduced (0.0f, inner.getHeight() * 0.34f), 1.5f);
        }

        if (textToShow.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffd4dfdb));
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (textToShow, r, juce::Justification::centred);
        }
    }

private:
    RetroHardware3D::Palette palette() const
    {
        return { findColour (primaryLed), findColour (secondaryLed), findColour (tertiaryLed) };
    }

    int paletteIndex = 0;
};
