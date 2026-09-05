#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

class ModularModPage final : public juce::Component, private juce::Timer
{
public:
    explicit ModularModPage (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto prefix = "lfoModule" + juce::String (i + 2);
            auto& rate = rates[(size_t) i]; auto& shape = shapes[(size_t) i];
            addAndMakeVisible (rate); addAndMakeVisible (shape);
            rate.setSliderStyle (juce::Slider::LinearHorizontal); rate.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20); rate.setTextValueSuffix (" Hz");
            shape.addItemList ({ "Sine", "Triangle", "Square", "Ramp" }, 1);
            rateAttachments[(size_t) i] = std::make_unique<SliderAttachment> (proc.apvts, prefix + "Rate", rate);
            shapeAttachments[(size_t) i] = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Shape", shape);
        }
        for (int i = 0; i < 4; ++i)
        {
            const auto prefix = "moduleMod" + juce::String (i + 1);
            auto& source = sources[(size_t) i]; auto& destination = destinations[(size_t) i]; auto& amount = amounts[(size_t) i];
            addAndMakeVisible (source); addAndMakeVisible (destination); addAndMakeVisible (amount);
            source.addItemList ({ "Off", "LFO 1", "Velocity", "Key Track", "Random Note", "Amp Env", "MSEG", "LFO 2", "LFO 3", "LFO 4" }, 1);
            destination.addItemList ({ "Off", "Pitch", "Cutoff", "Amplitude", "Pulse Width", "FM Amount", "6-OP FM Mix", "Wavetable Position", "Wavefold" }, 1);
            amount.setSliderStyle (juce::Slider::LinearHorizontal); amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 24);
            sourceAttachments[(size_t) i] = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Source", source);
            destinationAttachments[(size_t) i] = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Dest", destination);
            amountAttachments[(size_t) i] = std::make_unique<SliderAttachment> (proc.apvts, prefix + "Amount", amount);
        }
        startTimerHz (20);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16); r.removeFromTop (40); auto lfos = r.removeFromTop (210); const int w = lfos.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto column = lfos.removeFromLeft (w).reduced (5); plots[(size_t) i] = column.removeFromTop (130);
            shapes[(size_t) i].setBounds (column.removeFromTop (28).reduced (2)); rates[(size_t) i].setBounds (column.reduced (2));
        }
        r.removeFromTop (40);
        for (int i = 0; i < 4; ++i)
        {
            auto row = r.removeFromTop (42); sources[(size_t) i].setBounds (row.removeFromLeft (r.getWidth() / 3).reduced (3));
            destinations[(size_t) i].setBounds (row.removeFromLeft (r.getWidth() / 3).reduced (3)); amounts[(size_t) i].setBounds (row.reduced (3));
        }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719)); const auto led = findColour (RetroLookAndFeel::primaryLed); g.setColour (led); g.setFont (14);
        g.drawText ("INDEPENDENT LFO MODULES > MODULATION ROUTES", 16, 10, getWidth() - 32, 28, juce::Justification::centredLeft);
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
            g.setColour (led.withAlpha (0.4f)); g.drawLine (plots[(size_t) k].getCentreX(), plots[(size_t) k].getBottom() + 78.0f, getWidth() * 0.5f, 280.0f);
        }
        g.setColour (led); g.setFont (12); g.drawText ("SOURCE                                DESTINATION                                  DEPTH", 20, 278, getWidth() - 40, 26, juce::Justification::centredLeft);
    }
private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    RetroMatchSynthAudioProcessor& proc;
    std::array<juce::Rectangle<int>, 3> plots;
    std::array<juce::Slider, 3> rates; std::array<juce::ComboBox, 3> shapes;
    std::array<juce::ComboBox, 4> sources, destinations; std::array<juce::Slider, 4> amounts;
    std::array<std::unique_ptr<SliderAttachment>, 3> rateAttachments;
    std::array<std::unique_ptr<ComboAttachment>, 3> shapeAttachments;
    std::array<std::unique_ptr<SliderAttachment>, 4> amountAttachments;
    std::array<std::unique_ptr<ComboAttachment>, 4> sourceAttachments, destinationAttachments;
    void timerCallback() override { repaint(); }
};

class ModulatorsPage final : public juce::Component
{
public:
    ModulatorsPage (RetroMatchSynthAudioProcessor& p, juce::Component* builtIn) : modules (p)
    {
        addAndMakeVisible (tabs); tabs.addTab ("LFO MODULES + ROUTING", juce::Colour (0xff101719), &modules, false);
        tabs.addTab ("BUILT-IN MOD", juce::Colour (0xff101719), builtIn, false);
    }
    void resized() override { tabs.setBounds (getLocalBounds()); }
private:
    ModularModPage modules; juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
