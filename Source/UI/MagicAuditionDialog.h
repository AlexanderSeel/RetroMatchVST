#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

// A small model-backed Magic review surface. The processor owns preview and
// branch state; this dialog exposes the auditable facts and commit actions.
class MagicAuditionDialog final : public juce::Component, private juce::Timer
{
public:
    explicit MagicAuditionDialog (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        setLookAndFeel (&laf); setOpaque (true);
        for (auto* button : { &keep, &apply, &restore, &branch }) addAndMakeVisible (*button);
        addAndMakeVisible (report);
        report.setMultiLine (true); report.setReadOnly (true); report.setScrollbarsShown (false);
        report.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff071012));
        report.setColour (juce::TextEditor::textColourId, juce::Colour (0xffc5d6d1));
        keep.setButtonText ("KEEP PREVIEW"); apply.setButtonText ("APPLY / NEW ORIGIN");
        restore.setButtonText ("RESTORE ORIGIN"); branch.setButtonText ("RESTORE LAST BRANCH");
        keep.onClick = [this] { proc.keepMagicPreview(); refresh (true); };
        apply.onClick = [this] { proc.applyMagicPreview(); refresh (true); };
        restore.onClick = [this] { proc.restoreMagicOrigin(); refresh (true); };
        branch.onClick = [this]
        {
            const int count = proc.getMagicBranchCount();
            if (count > 0) proc.restoreMagicBranch (count - 1);
            refresh (true);
        };
        startTimerHz (10); refresh (true);
    }

    ~MagicAuditionDialog() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff101719)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        report.setBounds (area.removeFromTop (getHeight() - 82));
        auto actions = area.removeFromTop (34); const int width = actions.getWidth() / 4;
        keep.setBounds (actions.removeFromLeft (width).reduced (2));
        apply.setBounds (actions.removeFromLeft (width).reduced (2));
        restore.setBounds (actions.removeFromLeft (width).reduced (2));
        branch.setBounds (actions.reduced (2));
    }

private:
    RetroMatchSynthAudioProcessor& proc;
    RetroLookAndFeel laf;
    juce::TextEditor report;
    juce::TextButton keep, apply, restore, branch;
    bool lastPreviewState = false;
    int lastBranchCount = -1;
    juce::String lastRenderReport { "not measured" };

    void timerCallback() override
    {
        const bool preview = proc.hasMagicPreview();
        const int branches = proc.getMagicBranchCount();
        refresh (preview != lastPreviewState || branches != lastBranchCount);
    }

    void refresh (bool updateRender)
    {
        const bool preview = proc.hasMagicPreview();
        const int branches = proc.getMagicBranchCount();
        if (updateRender) lastRenderReport = proc.getMagicRenderReport();
        lastPreviewState = preview; lastBranchCount = branches;
        const auto state = preview ? "LIVE PREVIEW" : "WORKING PATCH";
        report.setText ("MAGIC AUDITION / " + juce::String (state) + "\n\n"
                        + "Origin distance: " + juce::String (proc.getMagicOriginDistance() * 100.0f, 1) + "%\n"
                        + "Changed dimensions: " + proc.getMagicChangedDimensions().joinIntoString (", ") + "\n"
                        + "Branches available: " + juce::String (branches) + "\n"
                        + "Render: " + lastRenderReport + "\n\n"
                        + "KEEP retains the current origin trail. APPLY commits this preview and starts a new origin.", false);
        keep.setEnabled (proc.hasMagicPreview()); apply.setEnabled (proc.hasMagicPreview());
        restore.setEnabled (proc.hasMagicOrigin()); branch.setEnabled (branches > 0);
    }
};
