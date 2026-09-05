#pragma once
#include <JuceHeader.h>
#include "RetroLookAndFeel.h"
#include <array>
#include <memory>
#include "../Engine/SynthEngine.h"

class MsegPage final : public juce::Component
{
public:
    explicit MsegPage (juce::AudioProcessorValueTreeState& stateIn) : state (stateIn)
    {
        setOpaque (true);

        enabled.setButtonText ("ENABLE MSEG 1");
        loopEnabled.setButtonText ("LOOP WHILE NOTE HELD");
        enabledAttachment = std::make_unique<ButtonAttachment> (state, "msegEnabled", enabled);
        loopAttachment = std::make_unique<ButtonAttachment> (state, "msegLoopEnabled", loopEnabled);

        styleLabel (loopStartLabel, "LOOP START");
        styleLabel (loopEndLabel, "LOOP END");
        loopStart.addItemList ({ "P1", "P2", "P3", "P4", "P5" }, 1);
        loopEnd.addItemList ({ "P2", "P3", "P4", "P5", "P6" }, 1);
        loopStartAttachment = std::make_unique<ComboAttachment> (state, "msegLoopStart", loopStart);
        loopEndAttachment = std::make_unique<ComboAttachment> (state, "msegLoopEnd", loopEnd);

        addAndMakeVisible (enabled);
        addAndMakeVisible (loopEnabled);
        addAndMakeVisible (loopStartLabel);
        addAndMakeVisible (loopStart);
        addAndMakeVisible (loopEndLabel);
        addAndMakeVisible (loopEnd);

        for (int i = 0; i < MsegParameters::pointCount; ++i)
        {
            const auto number = juce::String (i + 1);
            styleLabel (pointLabels[(size_t) i], "POINT " + number);
            auto& slider = pointLevels[(size_t) i];
            configureRotary (slider, 2);
            slider.onValueChange = [this] { repaint (graphBounds); };
            pointAttachments[(size_t) i] = std::make_unique<SliderAttachment> (state, "msegLevel" + number, slider);
            addAndMakeVisible (pointLabels[(size_t) i]);
            addAndMakeVisible (slider);
        }

        for (int i = 0; i < MsegParameters::segmentCount; ++i)
        {
            const auto number = juce::String (i + 1);
            styleLabel (timeLabels[(size_t) i], "SEG " + number + " TIME");
            auto& time = segmentTimes[(size_t) i];
            configureRotary (time, 3);
            time.setTextValueSuffix (" s");
            time.onValueChange = [this] { repaint (graphBounds); };
            timeAttachments[(size_t) i] = std::make_unique<SliderAttachment> (state, "msegTime" + number, time);

            styleLabel (curveLabels[(size_t) i], "CURVE " + number);
            auto& curve = segmentCurves[(size_t) i];
            configureRotary (curve, 2);
            curve.onValueChange = [this] { repaint (graphBounds); };
            curveAttachments[(size_t) i] = std::make_unique<SliderAttachment> (state, "msegCurve" + number, curve);

            addAndMakeVisible (timeLabels[(size_t) i]);
            addAndMakeVisible (time);
            addAndMakeVisible (curveLabels[(size_t) i]);
            addAndMakeVisible (curve);
        }

        const juce::StringArray sources { "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG 1" };
        const juce::StringArray destinations { "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "PM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" };
        for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
        {
            const auto number = juce::String (i + 1);
            styleLabel (routeLabels[(size_t) i], "GRAPH " + number);
            routeSources[(size_t) i].addItemList (sources, 1);
            routeDestinations[(size_t) i].addItemList (destinations, 1);

            auto& amount = routeAmounts[(size_t) i];
            amount.setSliderStyle (juce::Slider::LinearHorizontal);
            amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
            amount.setNumDecimalPlacesToDisplay (2);

            sourceAttachments[(size_t) i] = std::make_unique<ComboAttachment> (state, "modGraph" + number + "Source", routeSources[(size_t) i]);
            destinationAttachments[(size_t) i] = std::make_unique<ComboAttachment> (state, "modGraph" + number + "Dest", routeDestinations[(size_t) i]);
            amountAttachments[(size_t) i] = std::make_unique<SliderAttachment> (state, "modGraph" + number + "Amount", amount);

            addAndMakeVisible (routeLabels[(size_t) i]);
            addAndMakeVisible (routeSources[(size_t) i]);
            addAndMakeVisible (routeDestinations[(size_t) i]);
            addAndMakeVisible (amount);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b1012));

        auto outer = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff263034));
        g.drawRoundedRectangle (outer, 9.0f, 1.0f);

        drawSection (g, graphBounds.toFloat(), "MSEG 1  //  SIX-POINT ENVELOPE", findColour (RetroLookAndFeel::primaryLed));
        drawSection (g, shapeBounds.toFloat(), "SEGMENT TIME + CURVE", findColour (RetroLookAndFeel::secondaryLed));
        drawSection (g, routeBounds.toFloat(), "POST-1.0 MODULATION GRAPH", findColour (RetroLookAndFeel::tertiaryLed));

        auto graph = graphBounds.toFloat().reduced (15.0f, 30.0f).withTrimmedBottom (8.0f);
        if (graph.getWidth() < 40.0f || graph.getHeight() < 40.0f) return;

        for (int i = 1; i < 4; ++i)
        {
            const float y = graph.getY() + graph.getHeight() * (float) i / 4.0f;
            g.setColour (juce::Colour (0xff26383b).withAlpha (0.7f));
            g.drawHorizontalLine ((int) y, graph.getX(), graph.getRight());
        }

        float totalTime = 0.0f;
        std::array<float, MsegParameters::segmentCount> times {};
        for (int i = 0; i < MsegParameters::segmentCount; ++i)
        {
            times[(size_t) i] = juce::jmax (0.001f, (float) segmentTimes[(size_t) i].getValue());
            totalTime += times[(size_t) i];
        }
        totalTime = juce::jmax (0.001f, totalTime);

        std::array<juce::Point<float>, MsegParameters::pointCount> points {};
        float elapsed = 0.0f;
        for (int i = 0; i < MsegParameters::pointCount; ++i)
        {
            const float x = graph.getX() + graph.getWidth() * (elapsed / totalTime);
            const float level = juce::jlimit (0.0f, 1.0f, (float) pointLevels[(size_t) i].getValue());
            const float y = juce::jmap (level, 0.0f, 1.0f, graph.getBottom(), graph.getY());
            points[(size_t) i] = { x, y };
            if (i < MsegParameters::segmentCount) elapsed += times[(size_t) i];
        }

        juce::Path path;
        path.startNewSubPath (points[0]);
        for (int i = 0; i < MsegParameters::segmentCount; ++i)
        {
            const auto a = points[(size_t) i];
            const auto b = points[(size_t) i + 1];
            const float curve = juce::jlimit (-1.0f, 1.0f, (float) segmentCurves[(size_t) i].getValue());
            const float bend = curve * (b.getX() - a.getX()) * 0.30f;
            path.cubicTo (a.getX() + (b.getX() - a.getX()) * 0.34f + bend, a.getY(),
                          a.getX() + (b.getX() - a.getX()) * 0.66f - bend, b.getY(), b.getX(), b.getY());
        }

        g.setColour (findColour (RetroLookAndFeel::primaryLed).withAlpha (0.16f));
        g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (findColour (RetroLookAndFeel::primaryLed));
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (int i = 0; i < MsegParameters::pointCount; ++i)
        {
            const auto p = points[(size_t) i];
            g.setColour (findColour (RetroLookAndFeel::secondaryLed));
            g.fillEllipse (juce::Rectangle<float> (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f));
            g.setColour (juce::Colour (0xffdce7e2));
            g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
            g.drawText ("P" + juce::String (i + 1), (int) p.x - 11, (int) p.y - 21, 24, 14, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto header = area.removeFromTop (34);
        enabled.setBounds (header.removeFromLeft (150).reduced (2, 2));
        loopEnabled.setBounds (header.removeFromLeft (190).reduced (2, 2));
        loopStartLabel.setBounds (header.removeFromLeft (78));
        loopStart.setBounds (header.removeFromLeft (82).reduced (2, 2));
        loopEndLabel.setBounds (header.removeFromLeft (72));
        loopEnd.setBounds (header.removeFromLeft (82).reduced (2, 2));
        area.removeFromTop (5);

        const int graphHeight = juce::jlimit (150, 235, area.getHeight() / 3);
        graphBounds = area.removeFromTop (graphHeight);
        area.removeFromTop (7);

        const int pointControlHeight = 115;
        auto pointsArea = graphBounds.reduced (12).removeFromBottom (pointControlHeight);
        const int pointWidth = juce::jmax (1, pointsArea.getWidth() / MsegParameters::pointCount);
        for (int i = 0; i < MsegParameters::pointCount; ++i)
        {
            auto cell = pointsArea.removeFromLeft (i == MsegParameters::pointCount - 1 ? pointsArea.getWidth() : pointWidth).reduced (4, 0);
            pointLabels[(size_t) i].setBounds (cell.removeFromTop (17));
            pointLevels[(size_t) i].setBounds (cell);
        }

        const int shapeHeight = juce::jlimit (155, 205, area.getHeight() / 2);
        shapeBounds = area.removeFromTop (shapeHeight);
        area.removeFromTop (7);
        auto shape = shapeBounds.reduced (12).withTrimmedTop (25);
        const int segmentWidth = juce::jmax (1, shape.getWidth() / MsegParameters::segmentCount);
        for (int i = 0; i < MsegParameters::segmentCount; ++i)
        {
            auto cell = shape.removeFromLeft (i == MsegParameters::segmentCount - 1 ? shape.getWidth() : segmentWidth).reduced (4, 0);
            auto top = cell.removeFromTop (cell.getHeight() / 2);
            timeLabels[(size_t) i].setBounds (top.removeFromTop (17));
            segmentTimes[(size_t) i].setBounds (top);
            curveLabels[(size_t) i].setBounds (cell.removeFromTop (17));
            segmentCurves[(size_t) i].setBounds (cell);
        }

        routeBounds = area;
        auto routes = routeBounds.reduced (12).withTrimmedTop (28);
        const int rowHeight = juce::jmax (36, routes.getHeight() / VoiceParameters::modGraphSlotCount);
        for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
        {
            auto row = routes.removeFromTop (i == VoiceParameters::modGraphSlotCount - 1 ? routes.getHeight() : rowHeight).reduced (3, 3);
            routeLabels[(size_t) i].setBounds (row.removeFromLeft (70));
            routeSources[(size_t) i].setBounds (row.removeFromLeft (juce::jmax (140, row.getWidth() / 3)).reduced (3, 0));
            routeDestinations[(size_t) i].setBounds (row.removeFromLeft (juce::jmax (160, row.getWidth() / 2)).reduced (3, 0));
            routeAmounts[(size_t) i].setBounds (row.reduced (3, 0));
        }
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::AudioProcessorValueTreeState& state;
    juce::ToggleButton enabled, loopEnabled;
    juce::Label loopStartLabel, loopEndLabel;
    juce::ComboBox loopStart, loopEnd;

    std::array<juce::Label, MsegParameters::pointCount> pointLabels;
    std::array<juce::Slider, MsegParameters::pointCount> pointLevels;
    std::array<juce::Label, MsegParameters::segmentCount> timeLabels, curveLabels;
    std::array<juce::Slider, MsegParameters::segmentCount> segmentTimes, segmentCurves;

    std::array<juce::Label, VoiceParameters::modGraphSlotCount> routeLabels;
    std::array<juce::ComboBox, VoiceParameters::modGraphSlotCount> routeSources, routeDestinations;
    std::array<juce::Slider, VoiceParameters::modGraphSlotCount> routeAmounts;

    std::unique_ptr<ButtonAttachment> enabledAttachment, loopAttachment;
    std::unique_ptr<ComboAttachment> loopStartAttachment, loopEndAttachment;
    std::array<std::unique_ptr<SliderAttachment>, MsegParameters::pointCount> pointAttachments;
    std::array<std::unique_ptr<SliderAttachment>, MsegParameters::segmentCount> timeAttachments, curveAttachments;
    std::array<std::unique_ptr<ComboAttachment>, VoiceParameters::modGraphSlotCount> sourceAttachments, destinationAttachments;
    std::array<std::unique_ptr<SliderAttachment>, VoiceParameters::modGraphSlotCount> amountAttachments;

    juce::Rectangle<int> graphBounds, shapeBounds, routeBounds;

    static void styleLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, juce::Colour (0xffaebdb8));
        label.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    static void configureRotary (juce::Slider& slider, int decimals)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        slider.setMouseDragSensitivity (180);
        slider.setNumDecimalPlacesToDisplay (decimals);
    }

    static void drawSection (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, juce::Colour accent)
    {
        if (bounds.isEmpty()) return;
        g.setColour (juce::Colour (0xff101719));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (juce::Colour (0xff334145));
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (title, bounds.withHeight (24.0f).reduced (9.0f, 0.0f), juce::Justification::centredLeft);
    }
};
