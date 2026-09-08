#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include "ReferenceRegion.h"
#include "../AI/AISettings.h"
#include "../AI/AISeedProvider.h"
#include <array>
#include <atomic>
#include <tuple>
#include <vector>
class SynthInstanceVisual;

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

    class MidiLearnSlider final : public juce::Slider
    {
    public:
        MidiLearnSlider() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow) {}
        std::function<void()> onLearn, onClear;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isRightButtonDown()) { juce::Slider::mouseDown (e); return; }
            juce::PopupMenu menu; menu.addItem (1, "MIDI Learn (move a CC)"); menu.addItem (2, "Clear MIDI Mapping");
            auto learn = onLearn; auto clear = onClear;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [learn, clear] (int result)
            { if (result == 1 && learn) learn(); else if (result == 2 && clear) clear(); });
        }
    };

    class TabPage final : public juce::Component
    {
    public:
        TabPage() { setOpaque (true); setBufferedToImage (true); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff070b0d));
            auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            const RetroHardware3D::Palette palette { findColour (RetroLookAndFeel::primaryLed),
                                                     findColour (RetroLookAndFeel::secondaryLed),
                                                     findColour (RetroLookAndFeel::tertiaryLed) };
            RetroHardware3D::drawRecessedPanel (g, bounds, palette, 7.0f);
            auto face = bounds.reduced (6.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff151d20), face.getTopLeft(),
                                                     juce::Colour (0xff0a0f11), face.getBottomLeft(), false));
            g.fillRoundedRectangle (face, 4.0f);
            for (int y = (int) face.getY() + 4; y < (int) face.getBottom() - 4; y += 4)
            {
                g.setColour (((y / 4) % 3 == 0) ? juce::Colours::white.withAlpha (0.012f)
                                                : juce::Colours::black.withAlpha (0.035f));
                g.drawHorizontalLine (y, face.getX() + 5.0f, face.getRight() - 5.0f);
            }
        }
    };

    class CandidateButton final : public juce::Button
    {
    public:
        CandidateButton (juce::String codeIn, juce::String familyIn)
            : juce::Button (codeIn), code (std::move (codeIn)), family (std::move (familyIn)) {}
        CandidateButton (const CandidateButton&) = delete;
        CandidateButton& operator= (const CandidateButton&) = delete;

        void setResult (const MatchResult* newResult, bool isSelected);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String code, family;
        MatchResult result;
        bool hasResult = false;
        bool selected = false;
    };

    class FilterResponseGraph final : public juce::Component
    {
    public:
        explicit FilterResponseGraph (RetroMatchSynthAudioProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    private:
        RetroMatchSynthAudioProcessor& proc;
    };

    class EnvelopeGraph final : public juce::Component
    {
    public:
        explicit EnvelopeGraph (RetroMatchSynthAudioProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    private:
        RetroMatchSynthAudioProcessor& proc;
    };

    class LfoScope final : public juce::Component
    {
    public:
        explicit LfoScope (RetroMatchSynthAudioProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    private:
        RetroMatchSynthAudioProcessor& proc;
    };

    class StereoMeter final : public juce::Component
    {
    public:
        explicit StereoMeter (RetroMatchSynthAudioProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    private:
        RetroMatchSynthAudioProcessor& proc;
    };

    class CompactMasterMeter final : public juce::Component
    {
    public:
        explicit CompactMasterMeter (RetroMatchSynthAudioProcessor& p) : proc (p)
        {
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            if (bounds.getWidth() < 30.0f || bounds.getHeight() < 22.0f) return;

            const auto mint = findColour (RetroLookAndFeel::primaryLed);
            const auto amber = findColour (RetroLookAndFeel::secondaryLed);
            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), 4.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff11191b), bounds.getTopLeft(),
                                                     juce::Colour (0xff070c0e), bounds.getBottomLeft(), false));
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (juce::Colour (0xff4a5b5e));
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

            auto content = bounds.reduced (5.0f, 4.0f);
            const std::array<float, 2> peaks { proc.getOutputPeakLeft(), proc.getOutputPeakRight() };
            constexpr int segments = 12;
            const float labelW = 10.0f;
            const float dbW = 27.0f;
            const float rowGap = 3.0f;
            const float rowH = (content.getHeight() - rowGap) * 0.5f;

            for (int channel = 0; channel < 2; ++channel)
            {
                auto row = juce::Rectangle<float> (content.getX(), content.getY() + channel * (rowH + rowGap),
                                                   content.getWidth(), rowH);
                auto label = row.removeFromLeft (labelW);
                auto dbArea = row.removeFromRight (dbW);
                auto bar = row.reduced (2.0f, juce::jmax (0.0f, (row.getHeight() - 7.0f) * 0.5f));
                const float db = juce::jlimit (-60.0f, 3.0f,
                                               20.0f * std::log10 (juce::jmax (0.0001f, peaks[(size_t) channel])));
                const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -48.0f, 0.0f, 0.0f, 1.0f));
                const int lit = (int) std::ceil (norm * segments);
                const float gap = 1.25f;
                const float segmentW = (bar.getWidth() - gap * (segments - 1)) / segments;

                g.setColour (juce::Colour (0xffaab9b5));
                g.setFont (juce::Font (juce::FontOptions (7.5f, juce::Font::bold)));
                g.drawText (channel == 0 ? "L" : "R", label, juce::Justification::centredLeft);

                for (int i = 0; i < segments; ++i)
                {
                    auto seg = juce::Rectangle<float> (bar.getX() + i * (segmentW + gap), bar.getY(), segmentW, bar.getHeight());
                    auto colour = i >= 11 ? juce::Colour (0xffff6b5f)
                                          : (i >= 9 ? amber : mint);
                    if (i >= lit) colour = juce::Colour (0xff253134);
                    else
                    {
                        g.setColour (colour.withAlpha (0.14f));
                        g.fillRoundedRectangle (seg.expanded (1.0f), 1.0f);
                    }
                    g.setColour (colour);
                    g.fillRoundedRectangle (seg, 0.8f);
                }

                g.setColour (db > -1.0f ? juce::Colour (0xffff8b73) : juce::Colour (0xffb9c8c3));
                g.setFont (juce::Font (juce::FontOptions (7.2f, juce::Font::bold)));
                g.drawText (db <= -59.5f ? "-∞" : juce::String (db, 0), dbArea, juce::Justification::centredRight);
            }
        }

    private:
        RetroMatchSynthAudioProcessor& proc;
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
    FilterResponseGraph filterGraph;
    EnvelopeGraph envelopeGraph;
    LfoScope lfoScope;
    StereoMeter outputMeter;
    CompactMasterMeter masterMeter;

    juce::Label title, subtitle, status, instanceContext;
    juce::ComboBox instanceChoice, keyboardOctave;
    ReferenceRegion referenceRegion;
    std::array<bool, 17> heldTypingKeys {};
    int typingBaseNote = 48, displayedLayerMask = -1;
    void updateTypingKeyboard();
    juce::TextButton savePatch { "SAVE PATCH" }, loadPatch { "LOAD PATCH" }, exportPreview { "EXPORT WAV" };
    juce::TextButton keyboardToggle { "KEYS" };
    juce::TextButton lightSwitch { "LED: MINT" };
    juce::Slider masterOutput;
    juce::Label masterOutputLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterOutputAttachment;
    juce::Label resynthStrategyLabel, resynthComplexityLabel;
    juce::ComboBox resynthStrategyChoice, resynthComplexityChoice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> resynthStrategyAttachment, resynthComplexityAttachment;
    int displayedPalette = -1;
    std::unique_ptr<juce::Drawable> logo;
    juce::Rectangle<int> logoBounds;
    std::unique_ptr<juce::Component> melodyPage, signalPage;
    std::unique_ptr<SynthInstanceVisual> synthVisual;
    void updateLightPalette();

    // Persistent reference -> match -> audition workspace.
    juce::TextButton load { "LOAD REFERENCE" }, quick { "QUICK x3" }, refine { "REFINE x3" }, aiVariants { "AI x3" }, compareMatch { "COMPARE" };
    CandidateButton candidateA { "A", "NATURAL" }, candidateB { "B", "FM / HARMONIC" }, candidateC { "C", "WT / TEXTURE" };
    juce::Slider candidateMorph;
    juce::Label candidateMorphLabel;
    juce::Rectangle<int> workspaceBounds, analyzerBounds, pipelineBounds;

    // Detected/manual source pitch and reference-vs-synth A/B audition.
    juce::Label referencePitchInfo, referenceBaseNoteLabel, referenceLevelLabel;
    juce::ComboBox referenceBaseNoteChoice;
    juce::TextButton resetReferencePitch { "RESET" };
    juce::TextButton auditionSynth { "SYNTH" }, auditionReference { "REF SOLO" }, auditionMix { "MIX" };
    juce::Slider referenceLevel;
    juce::Slider regionStart, regionEnd;
    juce::Label regionStartLabel, regionEndLabel;
    juce::TextButton applyReferenceRegion { "APPLY" }, createReferenceTable { "CREATE WAVETABLE" }, chopReferenceTable { "CHOP TO WAVETABLE" };
    float shownRegionStart = -1.0f, shownRegionEnd = -1.0f;

    std::atomic<float> matchProgress { 0.0f };
    double progressDisplay = 0.0;
    juce::ProgressBar progressBar { progressDisplay };
    std::unique_ptr<VariantThread> worker;
    WorkMode activeWorkMode = WorkMode::quick;

    // Editing pages. Matching deliberately stays outside these tabs.
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    TabPage synthPage, fmPage, filterAmpPage, modPage, fxPage, settingsPage, aiLogPage;

    juce::Label synthOscSection, synthTextureSection;
    juce::Label fmCoreSection, fmOperatorsSection, fmDetailSection;
    juce::Label filterSection, ampSection;
    juce::Label modLfoSection, modMatrixSection;
    juce::Label fxDistortionSection, fxChorusSection, fxDelaySection, fxReverbSection;
    juce::ComboBox distortionChoice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> distortionAttachment;
    juce::Label aiSection, backendSection, privacySection, aiLogSection;

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

    // Full, scrollable AI diagnostics. Secrets are never added to this view.
    juce::Label aiLogHint;
    juce::TextEditor aiLog;
    juce::TextButton aiCopyLog { "COPY LOG" }, aiClearLog { "CLEAR LOG" };

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

    void configureReferenceAudition();
    void updateReferencePitchControls();
    void updateReferenceAuditionControls();

    void chooseFile();
    void chooseSavePatch();
    void chooseLoadPatch();
    void chooseExportPreview();

    void startVariantSearch (WorkMode);
    void runVariantSearch (WorkMode, VariantThread&);
    std::array<MatchResult, 3> createLocalVariants (bool refined, VariantThread&);
    void finishVariantSearch (std::array<MatchResult, 3>, const juce::String& sourceLabel,
                              const juce::String& error = {}, const juce::String& diagnostics = {});
    void updateCandidateButtons();
    void selectCandidate (int index);

    void syncAISettingsFromControls();
    void updateAIControlsFromSettings();
    void updateAIStatus();
    void setAILog (const juce::String& text);

    void rebindFmOperatorEditor();
    void timerCallback() override;

    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

    void drawAnalyzer (juce::Graphics&, juce::Rectangle<float>);
    void drawPipeline (juce::Graphics&, juce::Rectangle<float>);
    void drawWorkspaceBackground (juce::Graphics&);
};
