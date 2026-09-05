#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

class ModuleVisual final : public juce::Component
{
public:
    std::function<FxModuleParameters()> parameters;
    std::function<void(float, float)> onDrag;
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onDrag) onDrag (juce::jlimit (0.0f, 1.0f, e.position.x / juce::jmax (1, getWidth())),
                            juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / juce::jmax (1, getHeight())));
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2);
        const auto led = findColour (RetroLookAndFeel::primaryLed);
        g.setColour (juce::Colour (0xff061015)); g.fillRoundedRectangle (r, 6);
        g.setColour (led.withAlpha (0.15f));
        for (int i = 1; i < 5; ++i) { g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 5), r.getY(), r.getBottom()); g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 5), r.getX(), r.getRight()); }
        const auto p = parameters ? parameters() : FxModuleParameters {};
        auto plot = r.reduced (8).withTrimmedBottom (13);
        juce::Path curve;
        for (int i = 0; i < 180; ++i)
        {
            const float x = i / 179.0f, bipolar = 2 * x - 1; float y = 0;
            if (p.type == 1 || p.type == 2) { y = 1.0f / std::sqrt (1 + std::pow (std::pow (600.0f, x - p.amount), 4.0f)); if (p.type == 2) y = 1 - y; y = y * 1.6f - 0.8f; }
            else if (p.type == 3) y = std::tanh (bipolar * (1 + p.amount * 15));
            else if (p.type == 4) y = juce::jlimit (-1.0f, 1.0f, bipolar * (1 + p.amount * 15));
            else if (p.type == 5) y = std::sin (bipolar * (1 + p.amount * 15));
            else if (p.type == 8) y = std::pow (p.feedback * 0.9f, std::floor (x * 8)) * std::exp (-std::fmod (x * 8, 1.0f) * 20);
            else if (p.type == 9) y = std::exp (-x * (2 + (1 - p.amount) * 8)) * std::sin (x * 180);
            else if (p.type == 10) { const float steps = 2 + p.amount * 14; y = std::round (bipolar * steps) / steps; }
            else if (p.type == 13) { const float threshold = std::pow (10.0f, -48 * p.amount / 20); y = std::copysign (std::min (std::abs (bipolar), threshold) + std::max (0.0f, std::abs (bipolar) - threshold) / (1 + p.feedback * 19), bipolar); }
            else y = std::sin (x * (2 + p.rate * 10) * juce::MathConstants<float>::twoPi) * p.amount;
            const float px = plot.getX() + x * plot.getWidth(), py = plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * plot.getHeight() * 0.45f;
            if (i == 0) curve.startNewSubPath (px, py); else curve.lineTo (px, py);
        }
        g.setColour (led.withAlpha (0.15f)); g.strokePath (curve, juce::PathStrokeType (6));
        g.setColour (p.bypass ? juce::Colours::grey : led); g.strokePath (curve, juce::PathStrokeType (1.6f));
        g.setFont (10); g.drawText ("SHAPE PREVIEW / DRAG X: AMOUNT  Y: RATE", r.removeFromBottom (16), juce::Justification::centred);
    }
};

class ModularFxPage final : public juce::Component, private juce::Timer
{
public:
    explicit ModularFxPage (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        addAndMakeVisible (viewport); viewport.setViewedComponent (&content, false);
        addAndMakeVisible (addType); addAndMakeVisible (add); add.setButtonText ("+ ADD MODULE");
        for (size_t i = 1; i < fxModuleCatalog.size(); ++i) addType.addItem (fxModuleCatalog[i].name, (int) i);
        addType.setSelectedId (3);
        add.onClick = [this] { for (int i = 0; i < FxModuleParameters::slotCount; ++i) if (value (i, "Type") == 0) { set (i, "Type", (float) addType.getSelectedId()); break; } refresh(); };
        for (int i = 0; i < FxModuleParameters::slotCount; ++i)
        {
            auto& row = rows[(size_t) i];
            content.addAndMakeVisible (row.panel);
            for (auto* c : std::array<juce::Component*, 8> { &row.type, &row.stage, &row.bypass, &row.up, &row.down, &row.copy, &row.remove, &row.visual }) row.panel.addAndMakeVisible (*c);
            for (size_t k = 0; k < fxModuleCatalog.size(); ++k) row.type.addItem (fxModuleCatalog[k].name, (int) k + 1);
            row.stage.addItemList ({ "PRE", "POST" }, 1); row.bypass.setButtonText ("BYPASS");
            row.up.setButtonText ("UP"); row.down.setButtonText ("DN"); row.copy.setButtonText ("COPY"); row.remove.setButtonText ("X");
            const auto prefix = "fxModule" + juce::String (i + 1);
            row.typeAttachment = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Type", row.type);
            row.stageAttachment = std::make_unique<ComboAttachment> (proc.apvts, prefix + "Stage", row.stage);
            row.bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, prefix + "Bypass", row.bypass);
            for (int k = 0; k < 4; ++k)
            {
                auto& slider = row.controls[(size_t) k]; row.panel.addAndMakeVisible (slider); row.panel.addAndMakeVisible (row.labels[(size_t) k]);
                slider.setSliderStyle (juce::Slider::LinearVertical); slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 18);
                row.attachments[(size_t) k] = std::make_unique<SliderAttachment> (proc.apvts, prefix + suffixes[(size_t) k], slider);
            }
            row.up.onClick = [this, i] { for (int j = i - 1; j >= 0; --j) if (value (j, "Type") > 0) { swap (i, j); break; } };
            row.down.onClick = [this, i] { for (int j = i + 1; j < FxModuleParameters::slotCount; ++j) if (value (j, "Type") > 0) { swap (i, j); break; } };
            row.remove.onClick = [this, i] { set (i, "Type", 0); refresh(); };
            row.copy.onClick = [this, i] { for (int j = 0; j < FxModuleParameters::slotCount; ++j) if (value (j, "Type") == 0) { for (const auto* key : allKeys) set (j, key, value (i, key)); break; } refresh(); };
            row.visual.parameters = [this, i] { FxModuleParameters v; v.type = (int) value (i, "Type"); v.amount = value (i, "Amount"); v.rate = value (i, "Rate"); v.feedback = value (i, "Feedback"); v.bypass = value (i, "Bypass") > 0.5f; return v; };
            row.visual.onDrag = [this, i] (float x, float y) { set (i, "Amount", x); set (i, "Rate", y); rows[(size_t) i].visual.repaint(); };
        }
        refresh(); startTimerHz (15);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12); r.removeFromTop (70);
        auto tools = r.removeFromTop (32); addType.setBounds (tools.removeFromLeft (240).reduced (2)); add.setBounds (tools.removeFromLeft (160).reduced (2));
        r.removeFromTop (8); viewport.setBounds (r); layoutRows();
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719)); const auto led = findColour (RetroLookAndFeel::primaryLed); g.setColour (led); g.setFont (13);
        juce::String chain = "SYNTH  >  PRE: ";
        for (int stage = 0; stage < 2; ++stage)
        {
            bool any = false;
            for (int i = 0; i < FxModuleParameters::slotCount; ++i) if (value (i, "Type") > 0 && (int) value (i, "Stage") == stage && value (i, "Bypass") < 0.5f)
            { chain += "[" + juce::String (i + 1) + ": " + fxModuleCatalog[(size_t) juce::jlimit (0, 13, (int) value (i, "Type"))].name + "] > "; any = true; }
            if (! any) chain += "dry > ";
            chain += stage == 0 ? "BUILT-IN FX > POST: " : "OUTPUT";
        }
        g.drawFittedText (chain, getLocalBounds().reduced (14).removeFromTop (65), juce::Justification::centredLeft, 3);
    }
private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    inline static constexpr std::array<const char*, 4> suffixes {{ "Amount", "Rate", "Feedback", "Mix" }};
    inline static constexpr std::array<const char*, 7> allKeys {{ "Type", "Stage", "Bypass", "Amount", "Rate", "Feedback", "Mix" }};
    RetroMatchSynthAudioProcessor& proc;
    struct Row
    {
        juce::Component panel; juce::ComboBox type, stage; juce::ToggleButton bypass;
        juce::TextButton up, down, copy, remove; ModuleVisual visual;
        std::array<juce::Slider, 4> controls; std::array<juce::Label, 4> labels;
        std::unique_ptr<ComboAttachment> typeAttachment, stageAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
        std::array<std::unique_ptr<SliderAttachment>, 4> attachments;
    };
    juce::Component content; juce::Viewport viewport; juce::ComboBox addType; juce::TextButton add;
    std::array<Row, FxModuleParameters::slotCount> rows;
    float value (int i, const char* key) const { return proc.apvts.getRawParameterValue ("fxModule" + juce::String (i + 1) + key)->load(); }
    void set (int i, const char* key, float v) { auto* p = proc.apvts.getParameter ("fxModule" + juce::String (i + 1) + key); p->setValueNotifyingHost (p->convertTo0to1 (v)); }
    void swap (int a, int b) { for (const auto* key : allKeys) { const float old = value (a, key); set (a, key, value (b, key)); set (b, key, old); } refresh(); }
    void timerCallback() override { refresh(); }
    void refresh()
    {
        bool available = false;
        for (int i = 0; i < FxModuleParameters::slotCount; ++i)
        {
            const int type = juce::jlimit (0, 13, (int) value (i, "Type")); auto& row = rows[(size_t) i]; row.panel.setVisible (type != 0); available |= type == 0;
            const auto& d = fxModuleCatalog[(size_t) type]; const char* labels[] { d.amount, d.rate, d.feedback, "Wet / dry" };
            for (int k = 0; k < 4; ++k) row.labels[(size_t) k].setText (labels[k], juce::dontSendNotification);
            row.visual.repaint();
        }
        add.setEnabled (available); layoutRows(); repaint();
    }
    void layoutRows()
    {
        int y = 0; const int width = juce::jmax (360, viewport.getWidth() - 16);
        for (auto& row : rows) if (row.panel.isVisible())
        {
            row.panel.setBounds (0, y, width, 166); y += 176;
            auto r = row.panel.getLocalBounds(); auto header = r.removeFromTop (30);
            row.type.setBounds (header.removeFromLeft (juce::jmax (100, width - 320)).reduced (2)); row.stage.setBounds (header.removeFromLeft (70).reduced (2));
            row.bypass.setBounds (header.removeFromLeft (78)); row.up.setBounds (header.removeFromLeft (36).reduced (2)); row.down.setBounds (header.removeFromLeft (36).reduced (2));
            row.copy.setBounds (header.removeFromLeft (53).reduced (2)); row.remove.setBounds (header.reduced (2));
            row.visual.setBounds (r.removeFromLeft (width * 2 / 5).reduced (3)); const int w = r.getWidth() / 4;
            for (int k = 0; k < 4; ++k) { auto control = r.removeFromLeft (w).reduced (2); row.labels[(size_t) k].setBounds (control.removeFromTop (24)); row.controls[(size_t) k].setBounds (control); }
        }
        content.setSize (width, juce::jmax (y, viewport.getHeight()));
    }
};

class FxRackPage final : public juce::Component
{
public:
    FxRackPage (RetroMatchSynthAudioProcessor& p, juce::Component* builtIn) : rack (p)
    {
        addAndMakeVisible (tabs); tabs.addTab ("MODULE RACK", juce::Colour (0xff101719), &rack, false);
        tabs.addTab ("BUILT-IN FX", juce::Colour (0xff101719), builtIn, false);
    }
    void resized() override { tabs.setBounds (getLocalBounds()); }
private:
    ModularFxPage rack;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
