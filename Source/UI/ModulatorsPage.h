#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include "TempoSyncControls.h"

class ModularModPage final : public juce::Component, private juce::Timer
{
public:
    explicit ModularModPage (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        addAndMakeVisible (clearRoutes);
        clearRoutes.setButtonText ("CLEAR ROUTES");
        clearRoutes.setTooltip ("Reset all four modular routing rows to Off and zero depth. LFO settings are preserved.");
        clearRoutes.onClick = [this]
        {
            juce::PopupMenu confirm;
            confirm.addItem (1, "Clear all modular routes");
            confirm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clearRoutes), [this] (int result)
            {
                if (result != 1) return;
                for (int i = 0; i < 4; ++i)
                {
                    const auto prefix = "moduleMod" + juce::String (i + 1);
                    setParameterToDefault (prefix + "Source");
                    setParameterToDefault (prefix + "Dest");
                    setParameterToDefault (prefix + "Amount");
                }
                repaint();
            });
        };

        for (int i = 0; i < 3; ++i)
        {
            const auto prefix = "lfoModule" + juce::String (i + 2);
            auto& rate = rates[(size_t) i]; auto& shape = shapes[(size_t) i];
            addAndMakeVisible (rate); addAndMakeVisible (shape); addAndMakeVisible (rateLabels[(size_t) i]);
            rate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            rate.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
            rate.setTextValueSuffix (" Hz");
            rate.setMouseDragSensitivity (180);
            rate.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                      juce::MathConstants<float>::pi * 2.8f, true);
            rateLabels[(size_t) i].setText ("RATE", juce::dontSendNotification);
            rateLabels[(size_t) i].setJustificationType (juce::Justification::centred);
            rateLabels[(size_t) i].setColour (juce::Label::textColourId, juce::Colour (0xffb8c8c3));
            rateLabels[(size_t) i].setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            shape.addItemList ({ "Sine", "Triangle", "Square", "Ramp" }, 1);
            rateAttachments[(size_t) i] = std::make_unique<SliderAttachment> (proc.apvts, prefix + "Rate", rate);
            shapeAttachments[(size_t) i] = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Shape", shape);
            syncs[(size_t) i] = std::make_unique<TempoSyncSelector> (proc, "lfo" + juce::String (i + 2) + "Sync", "lfo" + juce::String (i + 2) + "Division");
            addAndMakeVisible (*syncs[(size_t) i]);
        }
        for (int i = 0; i < 4; ++i)
        {
            const auto prefix = "moduleMod" + juce::String (i + 1);
            auto& source = sources[(size_t) i]; auto& destination = destinations[(size_t) i]; auto& amount = amounts[(size_t) i];
            addAndMakeVisible (source); addAndMakeVisible (destination); addAndMakeVisible (amount);
            source.addItemList ({ "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG", "LFO 2", "LFO 3", "LFO 4" }, 1);
            destination.addItemList ({ "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "FM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" }, 1);
            amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
            amount.setMouseDragSensitivity (160);
            amount.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                        juce::MathConstants<float>::pi * 2.8f, true);
            sourceAttachments[(size_t) i] = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Source", source);
            destinationAttachments[(size_t) i] = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Dest", destination);
            amountAttachments[(size_t) i] = std::make_unique<SliderAttachment> (proc.apvts, prefix + "Amount", amount);
        }
        startTimerHz (20);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto header = r.removeFromTop (36);
        clearRoutes.setBounds (header.removeFromRight (125).reduced (2, 4));
        const int lfoHeight = juce::jlimit (210, 230, r.getHeight() - 188);
        auto lfos = r.removeFromTop (lfoHeight);
        const int columnWidth = lfos.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto column = lfos.removeFromLeft (i == 2 ? lfos.getWidth() : columnWidth).reduced (6, 3);
            const int plotHeight = juce::jmax (72, column.getHeight() - 132);
            plots[(size_t) i] = column.removeFromTop (plotHeight);
            column.removeFromTop (4);
            shapes[(size_t) i].setBounds (column.removeFromTop (28).reduced (2));
            rateLabels[(size_t) i].setBounds (column.removeFromTop (16));
            auto knobBand = column.removeFromTop (juce::jmax (56, column.getHeight() - 28));
            const int knobWidth = juce::jmin (96, knobBand.getWidth());
            rates[(size_t) i].setBounds (knobBand.withSizeKeepingCentre (knobWidth, knobBand.getHeight()));
            syncs[(size_t) i]->setBounds (column.removeFromTop (28).reduced (2));
        }

        r.removeFromTop (8);
        routesHeaderBounds = r.removeFromTop (22);
        const int routeHeight = juce::jmax (36, r.getHeight() / 4);
        for (int i = 0; i < 4; ++i)
        {
            auto row = r.removeFromTop (i == 3 ? r.getHeight() : routeHeight);
            const int third = juce::jmax (1, row.getWidth() / 3);
            sources[(size_t) i].setBounds (row.removeFromLeft (third).reduced (3, 4));
            destinations[(size_t) i].setBounds (row.removeFromLeft (third).reduced (3, 4));
            auto depth = row.reduced (3, 1);
            const int trimWidth = juce::jmin (142, depth.getWidth());
            amounts[(size_t) i].setBounds (depth.withWidth (trimWidth));
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719)); const auto led = findColour (RetroLookAndFeel::primaryLed); g.setColour (led); g.setFont (14);
        g.drawText ("INDEPENDENT LFO MODULES > MODULATION ROUTES", 16, 10, getWidth() - 180, 28, juce::Justification::centredLeft);
        for (int k = 0; k < 3; ++k)
        {
            auto r = plots[(size_t) k].toFloat(); g.setColour (juce::Colour (0xff061015)); g.fillRoundedRectangle (r, 7);
            g.setColour (led); g.drawText ("LFO " + juce::String (k + 2), r.removeFromTop (23), juce::Justification::centred);
            auto plot = r.reduced (8); juce::Path wave;
            for (int i = 0; i < 150; ++i)
            {
                const float x = i / 149.0f, phase = std::fmod (x * 2, 1.0f); const int shape = shapes[(size_t) k].getSelectedId() - 1;
                const float y = shape == 1 ? 1 - 4 * std::abs (phase - 0.5f) : shape == 2 ? (phase < 0.5f ? 1.0f : -1.0f) : shape == 3 ? phase * 2 - 1 : std::sin (phase * juce::MathConstants<float>::twoPi);
                const float px = plot.getX() + x * plot.getWidth(), py = plot.getCentreY() - y * plot.getHeight() * 0.42f;
                if (i == 0) wave.startNewSubPath (px, py); else wave.lineTo (px, py);
            }
            g.setColour (led.withAlpha (0.14f)); g.strokePath (wave, juce::PathStrokeType (6)); g.setColour (led); g.strokePath (wave, juce::PathStrokeType (1.5f));
            const float phase = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.001 * rates[(size_t) k].getValue(), 1.0);
            g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawVerticalLine ((int) (plot.getX() + phase * plot.getWidth()), plot.getY(), plot.getBottom());
        }
        g.setColour (led);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        auto header = routesHeaderBounds.toFloat();
        const float third = header.getWidth() / 3.0f;
        g.drawText ("SOURCE", header.withWidth (third).reduced (4.0f, 0.0f), juce::Justification::centredLeft);
        g.drawText ("DESTINATION", header.withTrimmedLeft (third).withWidth (third).reduced (4.0f, 0.0f), juce::Justification::centredLeft);
        g.drawText ("DEPTH", header.withTrimmedLeft (third * 2.0f).reduced (4.0f, 0.0f), juce::Justification::centredLeft);

        int active = 0;
        for (int i = 0; i < 4; ++i)
            if (sources[(size_t) i].getSelectedId() > 1 && destinations[(size_t) i].getSelectedId() > 1
                && std::abs (amounts[(size_t) i].getValue()) > 0.0001) ++active;
        g.setColour (led.withAlpha (0.52f));
        g.drawText (juce::String (active) + " / 4 ACTIVE", routesHeaderBounds.withTrimmedLeft (juce::jmax (0, routesHeaderBounds.getWidth() - 90)), juce::Justification::centredRight);
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void setParameterToDefault (const juce::String& id)
    {
        if (auto* parameter = proc.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
    }

    RetroMatchSynthAudioProcessor& proc;
    std::array<juce::Rectangle<int>, 3> plots;
    juce::Rectangle<int> routesHeaderBounds;
    std::array<juce::Slider, 3> rates; std::array<juce::ComboBox, 3> shapes;
    std::array<juce::Label, 3> rateLabels;
    std::array<std::unique_ptr<TempoSyncSelector>, 3> syncs;
    std::array<juce::ComboBox, 4> sources, destinations; std::array<juce::Slider, 4> amounts;
    std::array<std::unique_ptr<SliderAttachment>, 3> rateAttachments;
    std::array<std::unique_ptr<ComboAttachment>, 3> shapeAttachments;
    std::array<std::unique_ptr<SliderAttachment>, 4> amountAttachments;
    std::array<std::unique_ptr<ComboAttachment>, 4> sourceAttachments, destinationAttachments;
    juce::TextButton clearRoutes;
    void timerCallback() override { repaint(); }
};

class ModulatorsPage final : public juce::Component
{
public:
    ModulatorsPage (RetroMatchSynthAudioProcessor& p, juce::Component* builtIn)
        : tempo (p), lfo1Sync (p, "lfo1Sync", "lfo1Division", "LFO 1 SYNC"), modules (p)
    {
        addAndMakeVisible (tempo); addAndMakeVisible (lfo1Sync); addAndMakeVisible (tabs);
        tabs.addTab ("LFO MODULES + ROUTING", juce::Colour (0xff101719), &modules, false);
        tabs.addTab ("BUILT-IN MOD", juce::Colour (0xff101719), builtIn, false);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto clock = r.removeFromTop (38).reduced (8, 3);
        tempo.setBounds (clock.removeFromLeft (juce::jmax (330, getWidth() / 2)));
        lfo1Sync.setBounds (clock.removeFromLeft (200));
        tabs.setBounds (r);
    }
private:
    TempoSyncBar tempo;
    TempoSyncSelector lfo1Sync;
    ModularModPage modules;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};