#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/RetroLookAndFeel.h"
#include <array>
#include <atomic>
#include <tuple>
#include <vector>

class RetroMatchSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            public juce::FileDragAndDropTarget,
                                            private juce::Timer
{
public:
    explicit RetroMatchSynthAudioProcessorEditor (RetroMatchSynthAudioProcessor&);
    ~RetroMatchSynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    class MatchThread : public juce::Thread
    {
    public:
        explicit MatchThread (RetroMatchSynthAudioProcessorEditor& ownerIn);
        void run() override;

    private:
        RetroMatchSynthAudioProcessorEditor& owner;
    };

    class TabPage final : public juce::Component
    {
    public:
        TabPage() { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff0d1214));
            auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (juce::Colour (0xff252d30));
            g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
        }
    };

    RetroMatchSynthAudioProcessor& proc;
    RetroLookAndFeel laf;

    juce::Label title, status;

    juce::TextButton load { "LOAD SAMPLE" }, match { "QUICK MATCH" }, refine { "REFINE MATCH" };
    juce::TextButton savePatch { "SAVE PATCH" }, loadPatch { "LOAD PATCH" }, exportPreview { "EXPORT WAV" };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    TabPage synthPage, fmPage, filterAmpPage, modPage, fxPage, matchPage;

    juce::Label synthOscSection, synthTextureSection;
    juce::Label fmCoreSection, fmOperatorsSection, fmDetailSection;
    juce::Label filterSection, ampSection;
    juce::Label modLfoSection, modMatrixSection;
    juce::Label fxChorusSection, fxDelaySection, fxReverbSection;
    juce::Label matchCandidatesSection, matchLocksSection, matchHelp;

    juce::TextButton makeCandidates { "BUILD A/B/C" }, candidateA { "A" }, candidateB { "B" }, candidateC { "C" };
    juce::Slider candidateMorph;
    juce::Label candidateMorphLabel;

    juce::Label osc1Label, osc2Label, filterLabel, fmAlgorithmLabel;
    juce::ComboBox osc1Choice, osc2Choice, filterChoice, fmAlgorithmChoice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osc1Attachment, osc2Attachment, filterAttachment, fmAlgorithmAttachment;

    juce::Label fmOperatorEditLabel, fmModeLabel;
    juce::ComboBox fmOperatorEditChoice, fmModeChoice;
    std::array<juce::Label, 7> fmDetailLabels;
    std::array<juce::Slider, 7> fmDetailSliders;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fmModeAttachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 7> fmDetailAttachments;
    int selectedFmOperator = 0;

    std::array<juce::Label, VoiceParameters::modSlotCount> modSlotLabels;
    std::array<juce::ComboBox, VoiceParameters::modSlotCount> modSourceChoices, modDestinationChoices;
    std::array<juce::Slider, VoiceParameters::modSlotCount> modAmountSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, VoiceParameters::modSlotCount> modSourceAttachments, modDestinationAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, VoiceParameters::modSlotCount> modAmountAttachments;
    std::array<std::unique_ptr<juce::TextButton>, 7> matchLockButtons;

    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<juce::String> knobIds;

    juce::Rectangle<int> sidebarBounds;
    juce::Rectangle<int> analyzerBounds;

    std::atomic<float> matchProgress { 0.0f };
    double progressDisplay = 0.0;
    juce::ProgressBar progressBar { progressDisplay };
    std::unique_ptr<MatchThread> worker;

    void addKnob (const juce::String& id, const juce::String& name, const juce::String& suffix = {});
    int findKnobIndex (const juce::String& id) const;
    void moveKnobToPage (const juce::String& id, juce::Component& page);
    void configurePages();
    void configureSectionLabel (juce::Label&, const juce::String&, juce::Component&);
    void layoutPages();
    void layoutKnobGrid (const juce::StringArray& ids, juce::Rectangle<int> area, int maxColumns);
    void layoutFmDetailGrid (juce::Rectangle<int> area);

    void chooseFile();
    void applyQuickMatch();
    void startRefine();
    void finishRefine (MatchResult result);
    void chooseSavePatch();
    void chooseLoadPatch();
    void chooseExportPreview();
    void timerCallback() override;
    void syncMatchLocks();
    void rebindFmOperatorEditor();

    void drawAnalyzer (juce::Graphics&, juce::Rectangle<float>);
};
