#pragma once
#include "../PluginProcessor.h"
#include "../Matching/GeneratedRackGainPolicy.h"
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
        addAndMakeVisible (hint); hint.setText ("INSTANCE RACK / Generated resynthesis can stay classic or build a 4, 6 or 8-instance studio stack. Roles are voiced as body, air, foundation, motion, harmonic colour, width and texture instead of cloning the same patch. EDIT jumps any instance into the normal synth tabs.", juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (resynthLabel); resynthLabel.setText ("RESYNTH INSTANCES", juce::dontSendNotification); resynthLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (resynthInstances); resynthInstances.addItemList ({ "1 / SINGLE", "2 / LAYERED", "3 / DEEP LAYERED" }, 1);
        resynthAttachment = std::make_unique<ComboAttachment> (proc.apvts, "resynthInstances", resynthInstances);
        addAndMakeVisible (strategyLabel); strategyLabel.setText ("RESYNTH METHOD", juce::dontSendNotification); strategyLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (strategy); strategy.addItemList ({ "BALANCED HYBRID", "REFERENCE WAVETABLE", "SPECTRAL SUBTRACTIVE", "FM / HARMONIC", "LAYERED STUDIO", "TEXTURE / CHOP", "FX / GUITAR CHAIN" }, 1);
        strategyAttachment = std::make_unique<ComboAttachment> (proc.apvts, "resynthStrategy", strategy);
        strategy.setTooltip ("Changes the search topology. Reference Wavetable and Texture/Chop lean on extracted cycles; FX/Guitar Chain rebuilds an inspectable ordered pedal/amp-style rack and uses diagnostic excitation scoring.");
        addAndMakeVisible (complexityLabel); complexityLabel.setText ("STACK DEPTH", juce::dontSendNotification); complexityLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (complexity); complexity.addItemList ({ "CLASSIC / 1-3", "STUDIO / 4", "DEEP / 6", "MAXIMUM / 8" }, 1);
        complexityAttachment = std::make_unique<ComboAttachment> (proc.apvts, "resynthComplexity", complexity);
        complexity.setTooltip ("Studio and deeper modes add complementary sound-design roles at conservative levels instead of simply duplicating a candidate.");
        addAndMakeVisible (editMain); editMain.setButtonText ("EDIT INSTANCE 1 / MAIN"); editMain.onClick = [this] { editInstance (-1); };
        addAndMakeVisible (add); add.setButtonText ("+ ADD CURRENT SYNTH INSTANCE");
        add.onClick = [this] { for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) if (! proc.hasLayer (i)) { proc.captureLayer (i); break; } refresh(); };
        addAndMakeVisible (safeSum); safeSum.setButtonText ("SAFE SUM"); safeSum.onClick = [this] { applySafeSum(); };
        safeSum.setTooltip ("Reduce Main + additive layer gains as one group only when their worst-case coherent contribution exceeds the generated-rack headroom budget. Layer balance is preserved; non-additive Mix/Subtract/Multiply/Divide rows are untouched.");
        addAndMakeVisible (rackStatus); rackStatus.setJustificationType (juce::Justification::centredRight); rackStatus.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        addAndMakeVisible (mainGain); mainGain.setSliderStyle (juce::Slider::LinearHorizontal); mainGain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
        mainGain.setNumDecimalPlacesToDisplay (2); mainGain.setTooltip ("Instance 1 / Main level"); mainAttachment = std::make_unique<SliderAttachment> (proc.apvts, "mainLayerGain", mainGain);
        mainGain.textFromValueFunction = [] (double value) { return juce::String (value, 2); }; mainGain.updateText();
        addAndMakeVisible (mainVisual); mainVisual.parameters = [this] { return proc.getCurrentVoiceParameters(); };
        addAndMakeVisible (viewport); viewport.setViewedComponent (&content, false);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto& row = rows[i]; content.addAndMakeVisible (row.panel);
            for (auto* c : std::array<juce::Component*, 6> { &row.name, &row.enabled, &row.capture, &row.edit, &row.clear, &row.visual }) row.panel.addAndMakeVisible (*c);
            row.enabled.setButtonText ("ON"); row.capture.setButtonText ("REPLACE WITH EDITED"); row.edit.setButtonText ("EDIT INSTANCE"); row.clear.setButtonText ("REMOVE");
            const auto prefix = "layer" + juce::String ((int) i + 1);
            row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, prefix + "Enabled", row.enabled);
            row.panel.addAndMakeVisible (row.operation);
            row.operation.addItemList ({ "Add (+)", "Mix", "Subtract (-)", "Multiply (x)", "Divide (protected)" }, 1);
            row.operationAttachment = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Operation", row.operation);
            row.operation.setTooltip ("Combine this instance with the accumulated signal above it. Amount 0 bypasses the operation; 1 applies it fully. Divide uses a bounded, regularised denominator.");
            const char* suffix[] { "Gain", "Pan", "Tune", "Amount" }; const char* names[] { "LEVEL", "PAN", "TUNE", "AMOUNT" };
            for (int k = 0; k < 4; ++k)
            {
                auto& slider = row.controls[(size_t) k]; row.panel.addAndMakeVisible (slider); row.panel.addAndMakeVisible (row.labels[(size_t) k]); row.labels[(size_t) k].setText (names[k], juce::dontSendNotification);
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); slider.setNumDecimalPlacesToDisplay (2); slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
                if (k == 2) slider.setTextValueSuffix (" st");
                row.attachments[(size_t) k] = std::make_unique<SliderAttachment> (proc.apvts, prefix + suffix[k], slider);
                slider.textFromValueFunction = [k] (double value) { return juce::String (value, 2) + (k == 2 ? " st" : ""); };
                slider.updateText();
            }
            row.capture.onClick = [this, i] { proc.captureLayer ((int) i); refresh(); };
            row.edit.onClick = [this, i] { editInstance ((int) i); };
            row.clear.onClick = [this, i] { proc.clearLayer ((int) i); refresh(); };
            row.visual.parameters = [this, i] { auto p = proc.getLayerParameters ((int) i); return p ? *p : VoiceParameters {}; };
        }
        refresh(); startTimerHz (8);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff101719)); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14); hint.setBounds (r.removeFromTop (58));
        auto resynth = r.removeFromTop (30); resynthLabel.setBounds (resynth.removeFromLeft (150)); resynthInstances.setBounds (resynth.removeFromLeft (220).reduced (2));
        auto method = r.removeFromTop (30); strategyLabel.setBounds (method.removeFromLeft (150)); strategy.setBounds (method.removeFromLeft (260).reduced (2)); complexityLabel.setBounds (method.removeFromLeft (110)); complexity.setBounds (method.reduced (2));
        editMain.setBounds (r.removeFromTop (30).reduced (2));
        auto controls = r.removeFromTop (30); add.setBounds (controls.removeFromLeft (controls.getWidth() * 2 / 3).reduced (2)); mainGain.setBounds (controls.reduced (2));
        auto safety = r.removeFromTop (28); safeSum.setBounds (safety.removeFromLeft (105).reduced (2)); rackStatus.setBounds (safety.reduced (2));
        mainVisual.setBounds (r.removeFromTop (65)); r.removeFromTop (6); viewport.setBounds (r); layoutRows();
    }
private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    RetroMatchSynthAudioProcessor& proc;
    struct Row
    {
        juce::Component panel; juce::Label name; SynthInstanceVisual visual;
        juce::ToggleButton enabled; juce::TextButton capture, edit, clear;
        std::array<juce::Slider, 4> controls; std::array<juce::Label, 4> labels;
        juce::ComboBox operation; std::unique_ptr<ComboAttachment> operationAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
        std::array<std::unique_ptr<SliderAttachment>, 4> attachments;
    };
    juce::Label hint, resynthLabel, strategyLabel, complexityLabel, rackStatus;
    juce::ComboBox resynthInstances, strategy, complexity; juce::TextButton add, editMain, safeSum; juce::Slider mainGain; SynthInstanceVisual mainVisual;
    std::unique_ptr<SliderAttachment> mainAttachment;
    std::unique_ptr<ComboAttachment> resynthAttachment, strategyAttachment, complexityAttachment;
    juce::Component content; juce::Viewport viewport;
    std::array<Row, VoiceParameters::extraLayerCount> rows;
    void editInstance (int index)
    {
        proc.selectEditingLayer (index);
        for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (c)) { tabs->setCurrentTabIndex (tabs->getTabNames().indexOf ("SYNTH")); break; }
        refresh();
    }
    void setParameterPlain (const juce::String& id, float value)
    {
        if (auto* parameter = proc.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
    void applySafeSum()
    {
        auto rack = proc.getCurrentVoiceParameters();
        const float before = GeneratedRackGainPolicy::coherentContribution (rack);
        const float scale = GeneratedRackGainPolicy::apply (rack);
        if (scale >= 0.99999f)
        {
            hint.setText ("SAFE SUM: additive rack is already inside the coherent headroom budget.", juce::dontSendNotification);
            return;
        }

        setParameterPlain ("mainLayerGain", rack.mainLayerGain);
        for (size_t i = 0; i < rack.layers.size(); ++i)
            if (rack.layers[i] != nullptr && rack.layerOperation[i] == 0)
                setParameterPlain ("layer" + juce::String ((int) i + 1) + "Gain", rack.layerGain[i]);
        hint.setText ("SAFE SUM: coherent contribution reduced from " + juce::String (before, 2) + " to "
                      + juce::String (GeneratedRackGainPolicy::coherentContribution (rack), 2)
                      + " while preserving additive layer ratios.", juce::dontSendNotification);
        refresh();
    }
    void timerCallback() override { refresh(); }
    void refresh()
    {
        bool available = false;
        int activeInstances = 1;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const bool present = proc.hasLayer ((int) i); auto& row = rows[i]; row.panel.setVisible (present); available |= ! present;
            const juce::uint32 colours[] { 0xffffbd65, 0xffc9a0ff, 0xff78f1c4, 0xffff91b8, 0xffa6cf75, 0xff94aaff, 0xffff9673 };
            row.name.setColour (juce::Label::textColourId, juce::Colour (colours[i])); row.edit.setToggleState (proc.getEditingLayer() == (int) i, juce::dontSendNotification);
            row.name.setText ("SYNTH " + juce::String ((int) i + 2) + " / " + proc.getLayerName ((int) i), juce::dontSendNotification); row.visual.repaint();
            const auto enabledId = "layer" + juce::String ((int) i + 1) + "Enabled";
            if (present && proc.apvts.getRawParameterValue (enabledId)->load() >= 0.5f) ++activeInstances;
        }
        const auto rack = proc.getCurrentVoiceParameters();
        const float contribution = GeneratedRackGainPolicy::coherentContribution (rack);
        const bool overBudget = GeneratedRackGainPolicy::hasActiveAdditiveLayer (rack) && contribution > GeneratedRackGainPolicy::defaultCoherentBudget + 1.0e-5f;
        rackStatus.setText (juce::String (activeInstances) + " ACTIVE  /  COHERENT SUM " + juce::String (contribution, 2)
                            + (overBudget ? "  /  SAFE SUM RECOMMENDED" : "  /  HEADROOM OK"), juce::dontSendNotification);
        rackStatus.setColour (juce::Label::textColourId, overBudget ? findColour (RetroLookAndFeel::secondaryLed) : findColour (RetroLookAndFeel::primaryLed));
        safeSum.setEnabled (overBudget);
        add.setEnabled (available); mainVisual.repaint(); layoutRows();
    }
    void layoutRows()
    {
        int y = 0; const int width = juce::jmax (360, viewport.getWidth() - 16);
        for (auto& row : rows) if (row.panel.isVisible())
        {
            row.panel.setBounds (0, y, width, 248); y += 260;
            auto r = row.panel.getLocalBounds(); row.name.setBounds (r.removeFromTop (23));
            auto buttons = r.removeFromTop (30); row.enabled.setBounds (buttons.removeFromLeft (60)); const int w = buttons.getWidth() / 3;
            row.capture.setBounds (buttons.removeFromLeft (w).reduced (2)); row.edit.setBounds (buttons.removeFromLeft (w).reduced (2)); row.clear.setBounds (buttons.reduced (2));
            row.operation.setBounds (r.removeFromTop (30).reduced (2)); row.visual.setBounds (r.removeFromTop (58)); const int cw = r.getWidth() / 4;
            for (int k = 0; k < 4; ++k) { auto c = r.removeFromLeft (cw).reduced (2); row.labels[(size_t) k].setBounds (c.removeFromTop (18)); row.controls[(size_t) k].setBounds (c); }
        }
        content.setSize (width, juce::jmax (y, viewport.getHeight()));
    }
};