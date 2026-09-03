#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include "../AI/AISettings.h"
#include "../AI/AISeedProvider.h"
#include <array>
#include <atomic>
#include <tuple>
#include <vector>

class RetroMatchSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            public juce::FileDragAndDropTarget,
                                            private juce::Timer,
                                            private juce::MidiKeyboardState::Listener
{
public:
    explicit RetroMatchSynthAudioProcessorEditor (RetroMatchSynthAudioProcessor&);
    ~RetroMatchSynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    enum class WorkMode { quick, refine, ai };

    class TabPage final : public juce::Component
    {
    public:
        TabPage() { setOpaque (true); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff0b1012));
            auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (juce::Colour (0xff263034));
            g.drawRoundedRectangle (bounds, 9.0f, 1.0f);
        }
    };

    class CandidateButton final : public juce::Button
    {
    public:
        CandidateButton (juce::String codeIn, juce::String familyIn)
            : juce::Button (codeIn), code (std::move (codeIn)), family (std::move (familyIn)) {}

        void setResult (const MatchResult* newResult, bool isSelected);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String code, family;
        MatchResult result;
        bool hasResult = false;
        bool selected = false;
    };

    class VariantThread final : public juce::Thread
    {
    public:
        VariantThread (RetroMatchSynthAudioProcessorEditor& ownerIn, WorkMode modeIn);
        void run() override;

    private:
        RetroMatchSynthAudioProcessorEditor& owner;
        WorkMode mode;
    };

    RetroMatchSynthAudioProcessor& proc;
    RetroLookAndFeel laf;

    juce::Label title, subtitle, status;
    juce::TextButton savePatch { "SAVE PATCH" }, loadPatch { "LOAD PATCH" }, exportPreview { "EXPORT WAV" };
    juce::TextButton keyboardToggle { "KEYS" };

    // Persistent reference -> match -> audition workspace.
    juce::TextButton load { "LOAD REFERENCE" }, quick { "QUICK x3" }, refine { "REFINE x3" }, aiVariants { "AI x3" };
    CandidateButton candidateA { "A", "NATURAL" }, candidateB { "B", "FM / HARMONIC" }, candidateC { "C", "WT / TEXTURE" };
    juce::Slider candidateMorph;
    juce::Label candidateMorphLabel;
    juce::Rectangle<int> workspaceBounds, analyzerBounds, pipelineBounds;

    std::atomic<float> matchProgress { 0.0f };
    double progressDisplay = 0.0;
    juce::ProgressBar progressBar { progressDisplay };
    std::unique_ptr<VariantThread> worker;
    WorkMode activeWorkMode = WorkMode::quick;

    // Editing pages. Matching deliberately stays outside these tabs.
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    TabPage synthPage, fmPage, filterAmpPage, modPage, fxPage, settingsPage;

    juce::Label synthOscSection, synthTextureSection;
    juce::Label fmCoreSection, fmOperatorsSection, fmDetailSection;
    juce::Label filterSection, ampSection;
    juce::Label modLfoSection, modMatrixSection;
    juce::Label fxChorusSection, fxDelaySection, fxReverbSection;
    juce::Label aiSection, backendSection, privacySection;

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

    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<juce::String> knobIds;

    // AI settings: non-secret values persist, API keys remain session/env only.
    AISettings aiSettings;
    juce::ToggleButton aiEnabled { "ENABLE AI-ASSISTED SEEDS" };
    juce::ComboBox aiProvider;
    juce::Label aiProviderLabel, aiModelLabel, aiEndpointLabel, aiKeyEnvLabel, aiSessionKeyLabel, aiStatus;
    juce::TextEditor aiModel, aiEndpoint, aiKeyEnvironment, aiSessionKey;
    juce::TextButton aiSaveSettings { "SAVE AI SETTINGS" };
    juce::ComboBox resynthBackend;
    juce::Label resynthBackendLabel, resynthInfo, privacyInfo;

    // Optional audition keyboard.
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    bool keyboardVisible = true;

    void addKnob (const juce::String& id, const juce::String& name, const juce::String& suffix = {});
    int findKnobIndex (const juce::String& id) const;
    void moveKnobToPage (const juce::String& id, juce::Component& page);
    void configurePages();
    void configureSectionLabel (juce::Label&, const juce::String&, juce::Component&);
    void layoutPages();
    void layoutKnobGrid (const juce::StringArray& ids, juce::Rectangle<int> area, int maxColumns);
    void layoutFmDetailGrid (juce::Rectangle<int> area);

    void chooseFile();
    void chooseSavePatch();
    void chooseLoadPatch();
    void chooseExportPreview();

    void startVariantSearch (WorkMode);
    void runVariantSearch (WorkMode, VariantThread&);
    std::array<MatchResult, 3> createLocalVariants (bool refined, VariantThread&);
    void finishVariantSearch (std::array<MatchResult, 3>, const juce::String& sourceLabel, const juce::String& error = {});
    void updateCandidateButtons();
    void selectCandidate (int index);

    void syncAISettingsFromControls();
    void updateAIControlsFromSettings();
    void updateAIStatus();

    void rebindFmOperatorEditor();
    void timerCallback() override;

    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

    void drawAnalyzer (juce::Graphics&, juce::Rectangle<float>);
    void drawPipeline (juce::Graphics&, juce::Rectangle<float>);
    void drawWorkspaceBackground (juce::Graphics&);
};
