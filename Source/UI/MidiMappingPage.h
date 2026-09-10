#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

class MidiMappingPage final : public juce::Component, private juce::Timer
{
public:
    explicit MidiMappingPage (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        for (auto* button : { &previous, &next, &clear }) addAndMakeVisible (*button);

        previous.onClick = [this]
        {
            page = juce::jmax (0, page - 1);
            repaint();
        };
        next.onClick = [this]
        {
            page = juce::jmin (lastPage(), page + 1);
            repaint();
        };
        clear.onClick = [this]
        {
            if (proc.getMidiMappings().empty()) return;
            juce::PopupMenu confirm;
            confirm.addItem (1, "Remove all MIDI mappings");
            confirm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clear), [this] (int result)
            {
                if (result != 1) return;
                for (const auto& m : proc.getMidiMappings()) proc.removeMidiMapping (m.parameterId);
                page = 0;
                repaint();
            });
        };

        previous.setTooltip ("Show the previous page of MIDI mappings.");
        next.setTooltip ("Show the next page of MIDI mappings.");
        clear.setTooltip ("Remove every MIDI CC mapping after confirmation.");
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719));
        const auto led = findColour (RetroLookAndFeel::primaryLed);
        const auto mappings = proc.getMidiMappings();
        page = juce::jlimit (0, lastPage(), page);

        g.setColour (led);
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawText ("MIDI MAP  /  RIGHT-CLICK ANY KNOB TO LEARN", 18, 14, getWidth() - 370, 24, juce::Justification::centredLeft);

        g.setColour (led.withAlpha (0.12f));
        g.fillRoundedRectangle ((float) getWidth() - 346.0f, 13.0f, 82.0f, 26.0f, 5.0f);
        g.setColour (led.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String ((int) mappings.size()) + " MAPPED", getWidth() - 346, 13, 82, 26, juce::Justification::centred);

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        if (mappings.empty())
        {
            g.setColour (led.withAlpha (0.65f));
            g.drawText (proc.isMidiLearning() ? "LEARN ACTIVE  /  move a MIDI controller now..."
                                             : "No mapped controls yet. Right-click a synth control and choose MIDI Learn.",
                        20, 68, getWidth() - 40, 28, juce::Justification::centredLeft);
            return;
        }

        if (proc.isMidiLearning())
        {
            g.setColour (findColour (RetroLookAndFeel::secondaryLed).withAlpha (0.14f));
            g.fillRoundedRectangle (16.0f, 54.0f, (float) getWidth() - 32.0f, 32.0f, 5.0f);
            g.setColour (findColour (RetroLookAndFeel::secondaryLed));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText ("LEARN ACTIVE  /  MOVE A MIDI CC TO ASSIGN IT", 28, 59, getWidth() - 56, 20, juce::Justification::centredLeft);
        }

        const int start = page * rowsPerPage;
        const int end = juce::jmin ((int) mappings.size(), start + rowsPerPage);
        int y = 96;
        for (int i = start; i < end; ++i)
        {
            const auto& m = mappings[(size_t) i];
            g.setColour (juce::Colour (0xff17282c));
            g.fillRoundedRectangle (16.0f, (float) y, (float) getWidth() - 32.0f, 42.0f, 5.0f);

            juce::String displayName = m.parameterId;
            juce::String currentValue { "--" };
            if (auto* parameter = proc.apvts.getParameter (m.parameterId))
            {
                const auto friendly = parameter->getName (64);
                if (friendly.isNotEmpty()) displayName = friendly;
                currentValue = parameter->getCurrentValueAsText();
            }

            g.setColour (led.withAlpha (0.95f));
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (displayName, 28, y + 4, juce::jmax (160, getWidth() - 430), 19, juce::Justification::centredLeft);

            g.setColour (juce::Colour (0xff829592));
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.drawText (m.parameterId, 28, y + 22, juce::jmax (160, getWidth() - 430), 14, juce::Justification::centredLeft);

            const int infoX = juce::jmax (220, getWidth() - 390);
            g.setColour (juce::Colour (0xffb7c4c0));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText ("CC " + juce::String (m.cc + 1), infoX, y + 6, 72, 18, juce::Justification::centredLeft);
            g.setColour (led.withAlpha (0.72f));
            g.drawText ("NOW  " + currentValue, infoX + 78, y + 6, 150, 18, juce::Justification::centredLeft);
            g.setColour (led.withAlpha (0.48f));
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.drawText ("click row: relearn / delete", infoX, y + 23, 228, 14, juce::Justification::centredLeft);
            y += rowHeight;
        }

        if ((int) mappings.size() > rowsPerPage)
        {
            g.setColour (led.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText ("PAGE " + juce::String (page + 1) + " / " + juce::String (lastPage() + 1),
                        16, getHeight() - 42, getWidth() - 32, 24, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < 96) return;
        const auto mappings = proc.getMidiMappings();
        const int localIndex = (e.y - 96) / rowHeight;
        const int index = page * rowsPerPage + localIndex;
        if (localIndex < 0 || localIndex >= rowsPerPage || index < 0 || index >= (int) mappings.size()) return;

        juce::PopupMenu menu;
        menu.addItem (1, "Relearn CC");
        menu.addItem (2, "Delete mapping");
        const auto id = mappings[(size_t) index].parameterId;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, id] (int result)
        {
            if (result == 1) proc.beginMidiLearn (id);
            else if (result == 2) proc.removeMidiMapping (id);
            page = juce::jlimit (0, lastPage(), page);
            repaint();
        });
    }

    void resized() override
    {
        clear.setBounds (getWidth() - 150, 12, 130, 28);
        previous.setBounds (18, getHeight() - 38, 92, 26);
        next.setBounds (getWidth() - 110, getHeight() - 38, 92, 26);
    }

private:
    int lastPage() const
    {
        const int count = (int) proc.getMidiMappings().size();
        return count <= 0 ? 0 : (count - 1) / rowsPerPage;
    }

    void timerCallback() override
    {
        previous.setEnabled (page > 0);
        next.setEnabled (page < lastPage());
        clear.setEnabled (! proc.getMidiMappings().empty());
        repaint();
    }

    static constexpr int rowsPerPage = 8;
    static constexpr int rowHeight = 48;
    RetroMatchSynthAudioProcessor& proc;
    juce::TextButton previous { "< PREV" }, next { "NEXT >" }, clear { "CLEAR ALL" };
    int page = 0;
};
