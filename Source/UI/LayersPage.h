#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

class SynthInstanceVisual final : public juce::Component
{
public:
    std::function<VoiceParameters()> parameters;
    void paint (juce::Graphics& g) override
    {
        const auto p = parameters ? parameters() : VoiceParameters {};
        auto r = getLocalBounds().toFloat().reduced (2); const auto led = findColour (RetroLookAndFeel::primaryLed);
        g.setColour (juce::Colour (0xff061015)); g.fillRoundedRectangle (r, 7);
        auto footer = r.removeFromBottom (23); g.setColour (led); g.setFont (11);
        g.drawText ("OSC / FM / WAVETABLE  >  FILTER  >  ENVELOPE  >  PRE FX  >  POST FX", footer, juce::Justification::centred);
        auto wave = r.removeFromLeft (r.getWidth() * 0.55f).reduced (8);
        juce::Path path;
        for (int i = 0; i < 180; ++i)
        {
            const double phase = i / 179.0 * 3; const double frac = phase - std::floor (phase);
            float y = p.osc1Wave == 1 ? (float) (frac * 2 - 1) : p.osc1Wave == 2 ? (frac < p.pulseWidth ? 1.0f : -1.0f) : p.osc1Wave == 3 ? (float) (1 - 4 * std::abs (frac - 0.5)) : (float) std::sin (phase * juce::MathConstants<double>::twoPi);
            if (p.userWavetable && p.userWavetableMix > 0) y = p.userWavetable->sample (phase, p.wavetablePosition);
            else if (p.referenceWavetable && p.referenceWavetableMix > 0) y = p.referenceWavetable->sample (phase, p.wavetablePosition);
            const float x = wave.getX() + i * wave.getWidth() / 179.0f, py = wave.getCentreY() - y * wave.getHeight() * 0.4f;
            if (i == 0) path.startNewSubPath (x, py); else path.lineTo (x, py);
        }
        g.setColour (led.withAlpha (0.15f)); g.strokePath (path, juce::PathStrokeType (6)); g.setColour (led); g.strokePath (path, juce::PathStrokeType (1.5f));
        auto env = r.reduced (8); const float total = p.attack + p.decay + p.release + 0.3f;
        const float attack = env.getX() + env.getWidth() * p.attack / total, decay = attack + env.getWidth() * p.decay / total;
        const float release = env.getRight() - env.getWidth() * p.release / total, sustain = env.getBottom() - p.sustain * env.getHeight();
        juce::Path envelope; envelope.startNewSubPath (env.getX(), env.getBottom()); envelope.lineTo (attack, env.getY()); envelope.lineTo (decay, sustain); envelope.lineTo (release, sustain); envelope.lineTo (env.getRight(), env.getBottom());
        g.setColour (findColour (RetroLookAndFeel::secondaryLed)); g.strokePath (envelope, juce::PathStrokeType (2));
    }
};

class LayersPage final : public juce::Component, private juce::Timer
{
public:
    explicit LayersPage (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        addAndMakeVisible (hint); hint.setText ("SOURCE INSTANCES / up to 8 synths. Each copy keeps its oscillators, FM, wavetable, modulation and FX rack. Load a copy to edit in the main tabs, then save it back with COPY CURRENT.", juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (add); add.setButtonText ("+ ADD CURRENT SYNTH INSTANCE");
        add.onClick = [this] { for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) if (! proc.hasLayer (i)) { proc.captureLayer (i); break; } refresh(); };
        addAndMakeVisible (mainGain); mainGain.setSliderStyle (juce::Slider::LinearHorizontal); mainGain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
        mainGain.setTooltip ("Current synth level"); mainAttachment = std::make_unique<SliderAttachment> (proc.apvts, "mainLayerGain", mainGain);
        addAndMakeVisible (mainVisual); mainVisual.parameters = [this] { return proc.getMainVoiceParameters(); };
        addAndMakeVisible (viewport); viewport.setViewedComponent (&content, false);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto& row = rows[i]; content.addAndMakeVisible (row.panel);
            for (auto* c : std::array<juce::Component*, 6> { &row.name, &row.enabled, &row.capture, &row.edit, &row.clear, &row.visual }) row.panel.addAndMakeVisible (*c);
            row.enabled.setButtonText ("ON"); row.capture.setButtonText ("COPY CURRENT"); row.edit.setButtonText ("LOAD TO EDIT"); row.clear.setButtonText ("REMOVE");
            const auto prefix = "layer" + juce::String ((int) i + 1);
            row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, prefix + "Enabled", row.enabled);
            const char* suffix[] { "Gain", "Pan", "Tune" }; const char* names[] { "LEVEL", "PAN", "TUNE" };
            for (int k = 0; k < 3; ++k)
            {
                auto& slider = row.controls[(size_t) k]; row.panel.addAndMakeVisible (slider); row.panel.addAndMakeVisible (row.labels[(size_t) k]); row.labels[(size_t) k].setText (names[k], juce::dontSendNotification);
                slider.setSliderStyle (juce::Slider::LinearHorizontal); slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
                if (k == 2) slider.setTextValueSuffix (" st");
                row.attachments[(size_t) k] = std::make_unique<SliderAttachment> (proc.apvts, prefix + suffix[k], slider);
            }
            row.capture.onClick = [this, i] { proc.captureLayer ((int) i); refresh(); };
            row.edit.onClick = [this, i] { proc.loadLayerToMain ((int) i); };
            row.clear.onClick = [this, i] { proc.clearLayer ((int) i); refresh(); };
            row.visual.parameters = [this, i] { auto p = proc.getLayerParameters ((int) i); return p ? *p : VoiceParameters {}; };
        }
        refresh(); startTimerHz (8);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff101719)); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14); hint.setBounds (r.removeFromTop (60));
        auto controls = r.removeFromTop (30); add.setBounds (controls.removeFromLeft (controls.getWidth() * 2 / 3).reduced (2)); mainGain.setBounds (controls.reduced (2));
        mainVisual.setBounds (r.removeFromTop (110)); r.removeFromTop (6); viewport.setBounds (r); layoutRows();
    }
private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    RetroMatchSynthAudioProcessor& proc;
    struct Row
    {
        juce::Component panel; juce::Label name; SynthInstanceVisual visual;
        juce::ToggleButton enabled; juce::TextButton capture, edit, clear;
        std::array<juce::Slider, 3> controls; std::array<juce::Label, 3> labels;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
        std::array<std::unique_ptr<SliderAttachment>, 3> attachments;
    };
    juce::Label hint; juce::TextButton add; juce::Slider mainGain; SynthInstanceVisual mainVisual;
    std::unique_ptr<SliderAttachment> mainAttachment;
    juce::Component content; juce::Viewport viewport;
    std::array<Row, VoiceParameters::extraLayerCount> rows;
    void timerCallback() override { refresh(); }
    void refresh()
    {
        bool available = false;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const bool present = proc.hasLayer ((int) i); auto& row = rows[i]; row.panel.setVisible (present); available |= ! present;
            row.name.setText ("SYNTH " + juce::String ((int) i + 2) + " / " + proc.getLayerName ((int) i), juce::dontSendNotification); row.visual.repaint();
        }
        add.setEnabled (available); mainVisual.repaint(); layoutRows();
    }
    void layoutRows()
    {
        int y = 0; const int width = juce::jmax (360, viewport.getWidth() - 16);
        for (auto& row : rows) if (row.panel.isVisible())
        {
            row.panel.setBounds (0, y, width, 220); y += 230;
            auto r = row.panel.getLocalBounds(); row.name.setBounds (r.removeFromTop (23));
            auto buttons = r.removeFromTop (30); row.enabled.setBounds (buttons.removeFromLeft (60)); const int w = buttons.getWidth() / 3;
            row.capture.setBounds (buttons.removeFromLeft (w).reduced (2)); row.edit.setBounds (buttons.removeFromLeft (w).reduced (2)); row.clear.setBounds (buttons.reduced (2));
            row.visual.setBounds (r.removeFromTop (110)); const int cw = r.getWidth() / 3;
            for (int k = 0; k < 3; ++k) { auto c = r.removeFromLeft (cw).reduced (2); row.labels[(size_t) k].setBounds (c.removeFromTop (18)); row.controls[(size_t) k].setBounds (c); }
        }
        content.setSize (width, juce::jmax (y, viewport.getHeight()));
    }
};
