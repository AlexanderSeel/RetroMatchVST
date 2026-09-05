#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

class MidiMappingPage final : public juce::Component, private juce::Timer
{
public:
    explicit MidiMappingPage (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        addAndMakeVisible (clear); clear.onClick = [this] { for (const auto& m : proc.getMidiMappings()) proc.removeMidiMapping (m.parameterId); repaint(); };
        startTimerHz (8);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719));
        const auto led = findColour (RetroLookAndFeel::primaryLed);
        g.setColour (led); g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawText ("MIDI MAP  /  RIGHT-CLICK ANY KNOB TO LEARN", 18, 14, getWidth() - 36, 24, juce::Justification::centredLeft);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        const auto mappings = proc.getMidiMappings();
        if (mappings.empty()) { g.setColour (led.withAlpha (0.65f)); g.drawText (proc.isMidiLearning() ? "Move a MIDI CC now..." : "No mapped knobs yet.", 20, 68, getWidth() - 40, 24, juce::Justification::centredLeft); return; }
        int y = 62;
        for (const auto& m : mappings)
        {
            g.setColour (juce::Colour (0xff17282c)); g.fillRoundedRectangle (16.0f, (float) y, (float) getWidth() - 32.0f, 30.0f, 5.0f);
            g.setColour (led.withAlpha (0.9f)); g.drawText (m.parameterId, 28, y + 5, 210, 20, juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffb7c4c0)); g.drawText ("CC " + juce::String (m.cc + 1), 250, y + 5, 90, 20, juce::Justification::centredLeft);
            g.setColour (led.withAlpha (0.55f)); g.drawText ("click row to relearn / delete", 350, y + 5, getWidth() - 370, 20, juce::Justification::centredLeft);
            y += 36;
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int index = (e.y - 62) / 36; const auto mappings = proc.getMidiMappings();
        if (index < 0 || index >= (int) mappings.size()) return;
        juce::PopupMenu menu; menu.addItem (1, "Relearn CC"); menu.addItem (2, "Delete mapping");
        const auto id = mappings[(size_t) index].parameterId;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, id] (int result)
        { if (result == 1) proc.beginMidiLearn (id); else if (result == 2) proc.removeMidiMapping (id); repaint(); });
    }
    void resized() override { clear.setBounds (getWidth() - 150, 12, 130, 28); }
private:
    void timerCallback() override { repaint(); }
    RetroMatchSynthAudioProcessor& proc;
    juce::TextButton clear { "CLEAR ALL" };
};
