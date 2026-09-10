#pragma once
#include "../PluginProcessor.h"
#include "../Engine/PresetLibrary.h"
#include "LayersPage.h"

class PresetsPage final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit PresetsPage (RetroMatchSynthAudioProcessor& p) : proc (p), list ("Preset browser", this)
    {
        addAndMakeVisible (list); addAndMakeVisible (description); addAndMakeVisible (current); addAndMakeVisible (visual); addAndMakeVisible (autoLoad);
        list.setRowHeight (54); description.setMultiLine (true); description.setReadOnly (true); description.setScrollbarsShown (true);
        description.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff071012));
        description.setColour (juce::TextEditor::textColourId, juce::Colour (0xffb9c9c4));
        autoLoad.setButtonText ("LOAD ON SELECT"); autoLoad.setToggleState (true, juce::dontSendNotification);
        autoLoad.setTooltip ("When enabled, a single click loads the highlighted preset immediately. Double-click and LOAD SELECTED always work.");
        visual.parameters = [this] { return proc.getMainVoiceParameters(); };

        for (auto* b : { &load, &save, &open, &randomize, &audition, &favorite, &surprise, &clearSearch, &openFolder }) addAndMakeVisible (*b);
        load.setButtonText ("LOAD SELECTED"); save.setButtonText ("SAVE CURRENT"); open.setButtonText ("OPEN PRESET");
        randomize.setButtonText ("DESIGN NEW LAYERED PATCH"); audition.setButtonText ("AUDITION");
        favorite.setButtonText ("FAVORITE"); surprise.setButtonText ("SURPRISE ME"); clearSearch.setButtonText ("CLEAR"); openFolder.setButtonText ("FOLDER");
        favorite.setTooltip ("Add or remove the selected preset from Favorites. Favorites are stored locally in the RetroMatch preset folder.");
        surprise.setTooltip ("Select a random preset from the current filters. With LOAD ON SELECT enabled it is loaded immediately.");
        clearSearch.setTooltip ("Clear the preset search text.");
        openFolder.setTooltip ("Reveal the RetroMatch user preset folder in the operating system.");

        addAndMakeVisible (auditionNote);
        auditionNote.addItemList ({ "C2", "C3", "C4", "C5", "C6" }, 1);
        auditionNote.setSelectedId (3, juce::dontSendNotification);
        auditionNote.setTooltip ("Audition note used by the preset browser.");

        load.onClick = [this] { loadSelected(); };
        randomize.onClick = [this] { proc.randomizePreset(); refreshCurrent(); };
        audition.onClick = [this]
        {
            if (auditionUntil > 0) proc.noteOffFromEditor (auditionMidiNote);
            auditionMidiNote = 36 + (auditionNote.getSelectedId() - 1) * 12;
            proc.setReferenceAuditionMode (RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly);
            proc.noteOnFromEditor (auditionMidiNote, 0.75f); auditionUntil = juce::Time::getMillisecondCounterHiRes() + 1200;
        };
        favorite.onClick = [this] { toggleFavorite(); };
        surprise.onClick = [this]
        {
            if (visibleRows.empty()) return;
            list.selectRow (juce::Random::getSystemRandom().nextInt ((int) visibleRows.size()));
            list.scrollToEnsureRowIsOnscreen (list.getSelectedRow());
        };
        clearSearch.onClick = [this] { search.clear(); search.grabKeyboardFocus(); };
        openFolder.onClick = [this]
        {
            if (directory().createDirectory().wasOk()) directory().revealToUser();
            else description.setText ("Cannot create or reveal the user preset folder.", juce::dontSendNotification);
        };
        save.onClick = [this] { choose (true); }; open.onClick = [this] { choose (false); };

        addAndMakeVisible (search); search.setTextToShowWhenEmpty ("Search presets...", juce::Colours::grey);
        addAndMakeVisible (category); category.addItem ("All types", 1);
        juce::StringArray types;
        for (const auto& preset : factoryPresetCatalog) types.addIfNotAlreadyThere (preset.category);
        types.sort (true); category.addItemList (types, 2); category.addItem ("User", types.size() + 2);
        category.setSelectedId (1); category.onChange = [this] { filter(); }; search.onTextChange = [this] { filter(); };

        addAndMakeVisible (favoritesOnly); favoritesOnly.setButtonText ("FAVORITES ONLY");
        favoritesOnly.setTooltip ("Show only presets marked as favorites.");
        favoritesOnly.onClick = [this] { filter(); };
        addAndMakeVisible (stats);
        stats.setJustificationType (juce::Justification::centredRight);
        stats.setColour (juce::Label::textColourId, juce::Colour (0xff8fa5a0));
        stats.setFont (juce::Font (juce::FontOptions (10.5f)));

        loadFavorites(); rescan(); list.selectRow (0); refreshCurrent(); startTimerHz (10);
    }

    ~PresetsPage() override { if (auditionUntil > 0) proc.noteOffFromEditor (auditionMidiNote); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto titleRow = r.removeFromTop (30);
        current.setBounds (titleRow.removeFromLeft (juce::jmax (240, titleRow.getWidth() - 300)));
        stats.setBounds (titleRow.removeFromLeft (120));
        autoLoad.setBounds (titleRow.reduced (4, 2));
        r.removeFromTop (8);

        auto filters = r.removeFromTop (34);
        category.setBounds (filters.removeFromRight (170).reduced (2));
        favoritesOnly.setBounds (filters.removeFromRight (145).reduced (2));
        clearSearch.setBounds (filters.removeFromRight (62).reduced (2));
        search.setBounds (filters.reduced (2));
        r.removeFromTop (8);

        auto actions = r.removeFromTop (34); const int w = actions.getWidth() / 5;
        load.setBounds (actions.removeFromLeft (w).reduced (2));
        favorite.setBounds (actions.removeFromLeft (w).reduced (2));
        surprise.setBounds (actions.removeFromLeft (w).reduced (2));
        save.setBounds (actions.removeFromLeft (w).reduced (2));
        open.setBounds (actions.reduced (2));

        r.removeFromTop (8); auto bottom = r.removeFromBottom (36);
        randomize.setBounds (bottom.removeFromLeft (bottom.getWidth() * 5 / 12).reduced (2));
        auditionNote.setBounds (bottom.removeFromLeft (68).reduced (2));
        audition.setBounds (bottom.removeFromLeft (bottom.getWidth() * 2 / 3).reduced (2));
        openFolder.setBounds (bottom.reduced (2));
        list.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2)); r.removeFromLeft (10);
        visual.setBounds (r.removeFromTop (210)); description.setBounds (r.reduced (4, 8));
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff101719)); }

private:
    RetroMatchSynthAudioProcessor& proc; juce::ListBox list;
    juce::TextEditor search; juce::ComboBox category, auditionNote; std::vector<int> visibleRows;
    juce::TextEditor description; juce::Label current, stats; SynthInstanceVisual visual;
    juce::ToggleButton autoLoad, favoritesOnly;
    juce::TextButton load, save, open, randomize, audition, favorite, surprise, clearSearch, openFolder;
    juce::Array<juce::File> userFiles; std::unique_ptr<juce::FileChooser> chooser;
    juce::StringArray favoriteKeys;
    double auditionUntil = 0;
    int auditionMidiNote = 60;

    juce::File directory() const { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RetroMatch/Presets"); }
    juce::File favoritesFile() const { return directory().getChildFile ("favorites.txt"); }

    void loadFavorites()
    {
        favoriteKeys.clear();
        const auto file = favoritesFile();
        if (! file.existsAsFile()) return;
        favoriteKeys.addLines (file.loadFileAsString());
        favoriteKeys.trim(); favoriteKeys.removeEmptyStrings(); favoriteKeys.removeDuplicates (false);
    }

    void saveFavorites()
    {
        if (directory().createDirectory().failed()) return;
        favoritesFile().replaceWithText (favoriteKeys.joinIntoString ("\n") + (favoriteKeys.isEmpty() ? "" : "\n"));
    }

    juce::String keyForRow (int row) const
    {
        if (row < 0) return {};
        if (row < (int) factoryPresetCatalog.size()) return "factory:" + factoryPresetCatalog[(size_t) row].name;
        const int userIndex = row - (int) factoryPresetCatalog.size();
        if (userIndex < 0 || userIndex >= userFiles.size()) return {};
        return "user:" + userFiles[userIndex].getFileName();
    }

    bool isFavorite (int row) const { return favoriteKeys.contains (keyForRow (row)); }

    void toggleFavorite()
    {
        const int visible = list.getSelectedRow();
        if (visible < 0 || visible >= getNumRows()) return;
        const int row = visibleRows[(size_t) visible];
        const auto key = keyForRow (row);
        if (key.isEmpty()) return;
        const int existing = favoriteKeys.indexOf (key);
        if (existing >= 0) favoriteKeys.remove (existing); else favoriteKeys.add (key);
        saveFavorites();
        if (favoritesOnly.getToggleState()) filter(); else { updateFavoriteButton(); list.repaint(); }
    }

    void updateFavoriteButton()
    {
        const int visible = list.getSelectedRow();
        const bool valid = visible >= 0 && visible < getNumRows();
        favorite.setEnabled (valid);
        favorite.setButtonText (valid && isFavorite (visibleRows[(size_t) visible]) ? "UNFAVORITE" : "FAVORITE");
    }

    void rescan() { userFiles = directory().findChildFiles (juce::File::findFiles, false, "*.xml"); filter(); }

    void filter()
    {
        visibleRows.clear();
        for (int i = 0; i < (int) factoryPresetCatalog.size() + userFiles.size(); ++i)
        {
            const bool factory = i < (int) factoryPresetCatalog.size();
            const auto name = factory ? factoryPresetCatalog[(size_t) i].name : userFiles[i - (int) factoryPresetCatalog.size()].getFileNameWithoutExtension();
            const auto type = factory ? factoryPresetCatalog[(size_t) i].category : juce::String ("User");
            const bool categoryMatch = category.getSelectedId() == 1 || type == category.getText();
            const bool searchMatch = (name + " " + type).containsIgnoreCase (search.getText());
            if (categoryMatch && searchMatch && (! favoritesOnly.getToggleState() || isFavorite (i))) visibleRows.push_back (i);
        }
        std::stable_sort (visibleRows.begin(), visibleRows.end(), [] (int a, int b)
        {
            const auto count = (int) factoryPresetCatalog.size();
            if (a >= count || b >= count) return a < b;
            const auto& left = factoryPresetCatalog[(size_t) a]; const auto& right = factoryPresetCatalog[(size_t) b];
            const int categoryOrder = left.category.compareIgnoreCase (right.category);
            return categoryOrder == 0 ? left.name.compareIgnoreCase (right.name) < 0 : categoryOrder < 0;
        });
        list.deselectAllRows(); list.updateContent();
        if (! visibleRows.empty()) list.selectRow (0);
        else description.setText (favoritesOnly.getToggleState() ? "No favorite presets match this filter." : "No presets match this filter.", juce::dontSendNotification);
        load.setEnabled (! visibleRows.empty()); surprise.setEnabled (! visibleRows.empty());
        stats.setText (juce::String ((int) visibleRows.size()) + " shown / " + juce::String (favoriteKeys.size()) + " fav", juce::dontSendNotification);
        updateFavoriteButton();
    }

    int getNumRows() override { return (int) visibleRows.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (selected) { g.setColour (findColour (RetroLookAndFeel::primaryLed).withAlpha (0.15f)); g.fillRect (0, 0, width, height); }
        row = visibleRows[(size_t) row];
        const bool factory = row < (int) factoryPresetCatalog.size();
        const bool fav = isFavorite (row);
        g.setColour (findColour (RetroLookAndFeel::primaryLed)); g.setFont (14);
        const auto presetName = factory ? factoryPresetCatalog[(size_t) row].name : userFiles[row - (int) factoryPresetCatalog.size()].getFileNameWithoutExtension();
        g.drawText (presetName, 8, 3, width - 48, 22, juce::Justification::centredLeft);
        if (fav)
        {
            g.setColour (findColour (RetroLookAndFeel::secondaryLed)); g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
            g.drawText ("*", width - 34, 3, 24, 22, juce::Justification::centred);
        }
        g.setColour (juce::Colours::grey); g.setFont (10);
        g.drawText (factory ? "FACTORY / " + juce::String (factoryPresetCatalog[(size_t) row].category) : "USER PRESET", 8, 24, width - 16, 15, juce::Justification::centredLeft);
    }

    void selectedRowsChanged (int row) override
    {
        if (row < 0 || row >= getNumRows()) { updateFavoriteButton(); return; }
        updateDetails (visibleRows[(size_t) row]); updateFavoriteButton();
        if (autoLoad.getToggleState()) loadSelected();
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }

    void loadSelected()
    {
        int row = list.getSelectedRow(); if (row < 0 || row >= getNumRows()) return; row = visibleRows[(size_t) row];
        if (row < (int) factoryPresetCatalog.size()) proc.loadFactoryPreset (row);
        else if (! proc.loadPreset (userFiles[row - (int) factoryPresetCatalog.size()])) description.setText ("Could not load this preset.", juce::dontSendNotification);
        refreshCurrent();
    }

    void refreshCurrent()
    {
        current.setText ("CURRENT / " + proc.getPresetName(), juce::dontSendNotification);
        visual.repaint();
    }

    static juce::String waveName (int wave)
    {
        const juce::StringArray names { "Sine", "Saw", "Square", "Triangle", "Pulse" };
        return names[juce::jlimit (0, names.size() - 1, wave)];
    }

    static juce::String filterName (int type)
    {
        return type == 1 ? "High-pass" : type == 2 ? "Band-pass" : "Low-pass";
    }

    juce::String describePatch (const VoiceParameters& p, const juce::String& descriptionText) const
    {
        int layers = 1;
        for (const auto& layer : p.layers) if (layer) ++layers;
        juce::String s;
        if (descriptionText.isNotEmpty()) s << descriptionText << "\n\n";
        s << "SYNTHESIS\n"
          << "  " << layers << " instance" << (layers == 1 ? "" : "s")
          << "  |  OSC1 " << waveName (p.osc1Wave) << " " << juce::String (p.osc1Mix, 2)
          << "  |  OSC2 " << waveName (p.osc2Wave) << " " << juce::String (p.osc2Mix, 2) << "\n"
          << "  Wavetable " << juce::String (p.wavetableMix, 2)
          << "  |  Ref WT " << juce::String (p.referenceWavetableMix, 2)
          << "  |  FM " << juce::String (p.fmMix, 2) << " / algorithm " << juce::String (p.fmAlgorithm + 1)
          << "  |  Supersaw " << juce::String (p.supersawMix, 2) << "\n\n"
          << "FILTER + ENVELOPE\n"
          << "  " << filterName (p.filterType) << "  " << juce::String (p.cutoff, 0) << " Hz"
          << "  |  Resonance " << juce::String (p.resonance, 2) << "\n"
          << "  ADSR  " << juce::String (p.attack, 3) << " / " << juce::String (p.decay, 3)
          << " / " << juce::String (p.sustain, 2) << " / " << juce::String (p.release, 3) << " s\n\n"
          << "MOTION + SPACE\n"
          << "  MSEG " << (p.mseg.enabled ? "ON" : "off")
          << "  |  Chorus " << juce::String (p.chorusMix, 2)
          << "  |  Delay " << juce::String (p.delayMix, 2)
          << "  |  Reverb " << juce::String (p.reverbMix, 2)
          << "  |  Width " << juce::String (p.stereoWidth, 2) << "\n"
          << "  Patch output " << juce::String (p.outputGainDb, 1) << " dB";
        return s;
    }

    void updateDetails (int row)
    {
        if (row < 0) return;
        const juce::String fav = isFavorite (row) ? "FAVORITE / " : "";
        if (row < (int) factoryPresetCatalog.size())
        {
            const auto& info = factoryPresetCatalog[(size_t) row];
            description.setText (fav + "FACTORY / " + info.category + " / " + info.name + "\n\n"
                                 + describePatch (makeFactoryPreset (row), info.description), false);
        }
        else
        {
            const int userIndex = row - (int) factoryPresetCatalog.size();
            const auto file = userFiles[userIndex];
            description.setText (fav + "USER PRESET / " + file.getFileNameWithoutExtension() + "\n"
                                 + file.getFullPathName()
                                 + "\n\nSingle-click loads this preset when LOAD ON SELECT is active. "
                                   "After loading, the CURRENT display and synth visual show its complete state.", false);
        }
    }

    void timerCallback() override
    {
        if (auditionUntil > 0 && juce::Time::getMillisecondCounterHiRes() >= auditionUntil) { proc.noteOffFromEditor (auditionMidiNote); auditionUntil = 0; }
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
