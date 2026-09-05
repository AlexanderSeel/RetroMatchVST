#pragma once
#include "../PluginProcessor.h"
#include "../Engine/PresetLibrary.h"
#include "LayersPage.h"

class PresetsPage final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit PresetsPage (RetroMatchSynthAudioProcessor& p) : proc (p), list ("Preset browser", this)
    {
        addAndMakeVisible (list); addAndMakeVisible (description); addAndMakeVisible (current); addAndMakeVisible (visual);
        list.setRowHeight (44); description.setJustificationType (juce::Justification::topLeft);
        visual.parameters = [this] { return proc.getMainVoiceParameters(); };
        for (auto* b : { &load, &save, &open, &randomize, &audition }) addAndMakeVisible (*b);
        load.setButtonText ("LOAD SELECTED"); save.setButtonText ("SAVE CURRENT"); open.setButtonText ("OPEN PRESET");
        randomize.setButtonText ("RANDOMIZE NEW PATCH"); audition.setButtonText ("AUDITION");
        load.onClick = [this] { loadSelected(); };
        randomize.onClick = [this] { proc.randomizePreset(); refreshCurrent(); };
        audition.onClick = [this]
        {
            proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly);
            proc.noteOnFromEditor (60, 0.75f); auditionUntil = juce::Time::getMillisecondCounterHiRes() + 1200;
        };
        save.onClick = [this] { choose (true); }; open.onClick = [this] { choose (false); };
        rescan(); list.selectRow (0); refreshCurrent(); startTimerHz (10);
    }
    ~PresetsPage() override { if (auditionUntil > 0) proc.noteOffFromEditor (60); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16); current.setBounds (r.removeFromTop (30)); r.removeFromTop (8);
        auto actions = r.removeFromTop (34); const int w = actions.getWidth() / 3;
        load.setBounds (actions.removeFromLeft (w).reduced (2)); save.setBounds (actions.removeFromLeft (w).reduced (2)); open.setBounds (actions.reduced (2));
        r.removeFromTop (8); auto bottom = r.removeFromBottom (36);
        randomize.setBounds (bottom.removeFromLeft (bottom.getWidth() * 2 / 3).reduced (2)); audition.setBounds (bottom.reduced (2));
        list.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2)); r.removeFromLeft (10);
        visual.setBounds (r.removeFromTop (180)); description.setBounds (r.reduced (4, 12));
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff101719)); }
private:
    RetroMatchSynthAudioProcessor& proc; juce::ListBox list;
    juce::Label description, current; SynthInstanceVisual visual;
    juce::TextButton load, save, open, randomize, audition;
    juce::Array<juce::File> userFiles; std::unique_ptr<juce::FileChooser> chooser;
    double auditionUntil = 0;
    juce::File directory() const { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RetroMatch/Presets"); }
    void rescan() { userFiles = directory().findChildFiles (juce::File::findFiles, false, "*.xml"); list.updateContent(); }
    int getNumRows() override { return (int) factoryPresetCatalog.size() + userFiles.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (selected) { g.setColour (findColour (RetroLookAndFeel::primaryLed).withAlpha (0.15f)); g.fillRect (0, 0, width, height); }
        const bool factory = row < (int) factoryPresetCatalog.size();
        g.setColour (findColour (RetroLookAndFeel::primaryLed)); g.setFont (14);
        g.drawText (factory ? factoryPresetCatalog[(size_t) row].name : userFiles[row - (int) factoryPresetCatalog.size()].getFileNameWithoutExtension(), 8, 3, width - 16, 22, juce::Justification::centredLeft);
        g.setColour (juce::Colours::grey); g.setFont (10);
        g.drawText (factory ? "FACTORY / " + juce::String (factoryPresetCatalog[(size_t) row].category) : "USER PRESET", 8, 24, width - 16, 15, juce::Justification::centredLeft);
    }
    void selectedRowsChanged (int row) override
    {
        if (row >= 0 && row < (int) factoryPresetCatalog.size()) description.setText (factoryPresetCatalog[(size_t) row].description, juce::dontSendNotification);
        else description.setText ("Your saved synth instances, wavetables, modulation and FX chain.", juce::dontSendNotification);
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }
    void loadSelected()
    {
        const int row = list.getSelectedRow(); if (row < 0 || row >= getNumRows()) return;
        if (row < (int) factoryPresetCatalog.size()) proc.loadFactoryPreset (row);
        else if (! proc.loadPreset (userFiles[row - (int) factoryPresetCatalog.size()])) description.setText ("Could not load this preset.", juce::dontSendNotification);
        refreshCurrent();
    }
    void refreshCurrent() { current.setText ("CURRENT / " + proc.getPresetName(), juce::dontSendNotification); visual.repaint(); }
    void timerCallback() override
    {
        if (auditionUntil > 0 && juce::Time::getMillisecondCounterHiRes() >= auditionUntil) { proc.noteOffFromEditor (60); auditionUntil = 0; }
        refreshCurrent();
    }
    void choose (bool saving)
    {
        if (saving && directory().createDirectory().failed()) { description.setText ("Cannot create the user preset folder.", juce::dontSendNotification); return; }
        chooser = std::make_unique<juce::FileChooser> (saving ? "Save RetroMatch preset" : "Open RetroMatch preset", saving ? directory().getChildFile ("My Patch.xml") : directory(), "*.xml");
        juce::Component::SafePointer<PresetsPage> safe (this);
        chooser->launchAsync ((saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode) | juce::FileBrowserComponent::canSelectFiles,
            [safe, saving] (const juce::FileChooser& fc)
            {
                if (! safe || fc.getResult() == juce::File()) return;
                auto file = fc.getResult(); bool ok;
                if (saving) { file = file.withFileExtension ("xml"); safe->proc.apvts.state.setProperty ("patchName", file.getFileNameWithoutExtension(), nullptr); ok = safe->proc.savePreset (file); }
                else ok = safe->proc.loadPreset (file);
                safe->description.setText (ok ? (saving ? "Saved " : "Loaded ") + file.getFileName() : "Preset operation failed.", juce::dontSendNotification);
                safe->rescan(); safe->refreshCurrent();
            });
    }
};
