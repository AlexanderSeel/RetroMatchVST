#pragma once
#include <JuceHeader.h>
#include "Engine/SynthEngine.h"
#include "Engine/PatchGraph.h"
#include "Analysis/SampleAnalyzer.h"
#include "Matching/SoundMatcher.h"
#include "Matching/OfflineRenderer.h"
#include "Matching/CompareFineTune.h"
#include "Reference/ReferenceSamplePlayer.h"
#include <atomic>
#include "Engine/MelodyTransport.h"
#include "UI/AudioVisualBuffer.h"

class RetroMatchSynthAudioProcessor : public juce::AudioProcessor
{
public:
    struct MidiMapping { juce::String parameterId; int cc = 0; };
    enum class ReferenceAuditionMode : int
    {
        synthOnly = 0,
        referenceOnly,
        mixed
    };

    RetroMatchSynthAudioProcessor();
    ~RetroMatchSynthAudioProcessor() override = default;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::optional<SoundFeatures> currentFeatures;
    std::optional<SoundFeatures> currentCandidateFeatures;
    MatchResult lastMatch;
    juce::String loadedSampleName;
    MatchSettings matchSettings;
    std::shared_ptr<ReferenceWavetableData> referenceWavetable;
    std::shared_ptr<ReferenceWavetableData> userWavetable;
    juce::String userWavetableName;
    juce::String userWavetableDescription;
    std::array<MatchResult, 3> candidateBank {};
    int selectedCandidate = 0;
    std::atomic<int> lightPalette { 0 };
    AudioVisualBuffer visualAudio;
    MelodyTransport melodyTransport;
    juce::File getReferenceFile() const { return loadedReferenceFile; }
    float getReferenceAnalysisDuration() const noexcept { return analysisSourceDuration.load(); }
    float getAnalysisStartSeconds() const noexcept { return analysisStartSeconds.load(); }
    float getAnalysisEndSeconds() const noexcept { return analysisEndSeconds.load(); }
    float getMidiAnalysisStartSeconds() const noexcept;
    float getMidiAnalysisEndSeconds() const noexcept;
    void setMidiAnalysisRegion (float startSeconds, float endSeconds);
    float getEffectiveBpm() const noexcept { return effectiveBpm.load (std::memory_order_relaxed); }
    bool setReferenceAnalysisRegion (float startSeconds, float endSeconds);
    MelodyClip getMelodyClip() const { return MelodyClip::fromState (apvts.state.getChildWithName ("MELODY")); }
    void setMelodyClip (const MelodyClip& clip)
    {
        melodyTransport.stop();
        auto previous = apvts.state.getChildWithName ("MELODY");
        if (previous.isValid()) apvts.state.removeChild (previous, nullptr);
        apvts.state.appendChild (clip.toState(), nullptr);
    }
    PatchGraph::Document getPatchGraphDocument() const
    {
        return PatchGraph::Document::fromValueTree (apvts.state.getChildWithName ("PATCH_GRAPH"));
    }
    void setPatchGraphDocument (const PatchGraph::Document& graph)
    {
        const auto compiled = DspRouting::compile (graph);
        if (! compiled.validation.ok) return;
        auto previous = apvts.state.getChildWithName ("PATCH_GRAPH");
        if (previous.isValid()) apvts.state.removeChild (previous, nullptr);
        apvts.state.appendChild (graph.toValueTree(), nullptr);
        routingPlanPublisher.publish (compiled.plan);
    }
    RoutingMeterSnapshot getRoutingMeters() const noexcept { return engine.getRoutingMeters(); }
    void setSoloLayer (int layer) noexcept { engine.setSoloLayer (layer); }
    int getSoloLayer() const noexcept { return engine.getSoloLayer(); }
    void playMelody()
    {
        setReferenceAuditionMode (ReferenceAuditionMode::synthOnly);
        melodyTransport.start (getMelodyClip());
    }

    bool loadReferenceSample (const juce::File&);
    bool setReferenceBaseMidiNote (int midiNote);
    bool resetReferenceBaseMidiNote();
    int getReferenceBaseMidiNote() const noexcept { return referenceBaseMidiNote.load(); }
    int getDetectedReferenceMidiNote() const noexcept { return detectedReferenceMidiNote; }
    float getDetectedReferenceHz() const noexcept { return detectedReferenceHz; }
    float getDetectedReferencePitchConfidence() const noexcept { return detectedReferencePitchConfidence; }
    bool hasReferenceSample() const noexcept { return referencePlayer.hasSample(); }
    void beginMidiLearn (const juce::String& parameterId);
    void removeMidiMapping (const juce::String& parameterId);
    std::vector<MidiMapping> getMidiMappings() const;
    bool isMidiLearning() const noexcept { return midiLearning.load(); }

    bool loadUserWavetable (const juce::File&, int sourceFrameSize = 0);
    bool createUserWavetableFromReference (float startSeconds, float endSeconds, bool chop = false);
    void captureLayer (int index);
    void clearLayer (int index);
    bool loadLayerToMain (int index);
    void selectEditingLayer (int index);
    void refreshEditingLayer();
    int getEditingLayer() const { return editingLayer.load(); }
    bool hasLayer (int index) const;
    juce::String getLayerName (int index) const;
    std::shared_ptr<const VoiceParameters> getLayerParameters (int index) const
    {
        return juce::isPositiveAndBelow (index, VoiceParameters::extraLayerCount) ? savedLayers[(size_t) index].load() : nullptr;
    }
    void clearUserWavetable();
    bool hasUserWavetable() const noexcept { return userWavetable != nullptr && userWavetable->valid; }
    std::shared_ptr<const ReferenceWavetableData> getUserWavetable() const { return userWavetable; }
    const juce::String& getUserWavetableName() const noexcept { return userWavetableName; }
    const juce::String& getUserWavetableDescription() const noexcept { return userWavetableDescription; }

    static float midiNoteToHz (int midiNote);
    static int hzToNearestMidiNote (float hz);

    void setReferenceAuditionMode (ReferenceAuditionMode mode);
    ReferenceAuditionMode getReferenceAuditionMode() const noexcept
    {
        return static_cast<ReferenceAuditionMode> (referenceAuditionMode.load());
    }
    void setReferenceAuditionLevel (float level) noexcept { referenceAuditionLevel.store (juce::jlimit (0.0f, 1.0f, level)); }
    float getReferenceAuditionLevel() const noexcept { return referenceAuditionLevel.load(); }
    bool previewReferenceRegion (float startSeconds, float endSeconds, bool normalize, float fadeInSeconds, float fadeOutSeconds);
    void stopReferencePreview();
    bool exportReferenceSelection (const juce::File& destination, float startSeconds, float endSeconds,
                                   bool normalize, float fadeInSeconds, float fadeOutSeconds);

    bool previewReferenceRegionEdited (float startSeconds, float endSeconds, bool normalize,
                                       float fadeInSeconds, float fadeOutSeconds,
                                       const ReferenceSamplePlayer::EditTone& tone)
    {
        if (! loadedReferenceFile.existsAsFile()) return false;
        setReferenceAuditionMode (ReferenceAuditionMode::referenceOnly);
        return referencePlayer.previewRegion (loadedReferenceFile, referenceBaseMidiNote.load(),
                                              startSeconds, endSeconds, normalize,
                                              fadeInSeconds, fadeOutSeconds, tone);
    }

    bool exportReferenceSelectionEdited (const juce::File& destination, float startSeconds, float endSeconds,
                                         bool normalize, float fadeInSeconds, float fadeOutSeconds,
                                         const ReferenceSamplePlayer::EditTone& tone)
    {
        if (! loadedReferenceFile.existsAsFile()) return false;
        return ReferenceSamplePlayer::writeProcessedRegion (loadedReferenceFile, destination,
                                                            startSeconds, endSeconds, normalize,
                                                            fadeInSeconds, fadeOutSeconds, tone);
    }

    float getOutputPeakLeft() const noexcept { return outputPeakLeft.load (std::memory_order_relaxed); }
    float getOutputPeakRight() const noexcept { return outputPeakRight.load (std::memory_order_relaxed); }

    MatchResult fitReference();
    MatchResult refineReference (SoundMatcher::ProgressCallback progress = {}, SoundMatcher::CancelCallback cancel = {});
    void applyMatchResult (const MatchResult&);
    std::array<MatchResult, 3> buildCandidateBank();
    std::array<MatchResult, 3> buildGoldCandidateBank (SoundMatcher::ProgressCallback progress = {},
                                                       SoundMatcher::CancelCallback cancel = {});
    bool selectCandidate (int index);
    void morphCandidates (int a, int b, float amount);
    CompareFineTune::Values getCompareFineTuneValues() const noexcept
    {
        return juce::isPositiveAndBelow (selectedCandidate, 3)
             ? compareFineTuneValuesByCandidate[(size_t) selectedCandidate] : CompareFineTune::Values {};
    }
    const MatchResult* getSelectedCandidateBaseline() const noexcept
    {
        return juce::isPositiveAndBelow (selectedCandidate, 3) && candidateBank[(size_t) selectedCandidate].confidence > 0.0f
             ? &candidateBank[(size_t) selectedCandidate] : nullptr;
    }
    const MatchResult* getCompareFineTuneMeasuredResult() const noexcept
    {
        if (! juce::isPositiveAndBelow (selectedCandidate, 3)) return nullptr;
        const auto index = (size_t) selectedCandidate;
        if (! compareFineTuneMeasuredByCandidate[index]) return nullptr;
        return compareFineTuneValuesByCandidate[index].nearlyEquals (compareFineTuneMeasuredValuesByCandidate[index])
             ? &*compareFineTuneMeasuredByCandidate[index] : nullptr;
    }
    struct CompareFineTuneMeasureRequest
    {
        int candidateIndex = -1;
        CompareFineTune::Values values {};
        SoundFeatures reference;
        VoiceParameters params;
        MatchSettings settings;
        MatchResult baseline;
    };
    std::optional<CompareFineTuneMeasureRequest> makeCompareFineTuneMeasureRequest() const;
    bool acceptCompareFineTuneMeasurement (int candidateIndex, CompareFineTune::Values values, MatchResult measured);
    bool previewCompareFineTune (CompareFineTune::Values values);
    void resetCompareFineTune();
    bool showCompareFineTuneBaseline (bool baseline);
    bool measureCompareFineTune();
    bool keepCompareFineTune();
    bool isCompareFineTunePending() const noexcept { return compareFineTunePending; }
    bool isCompareFineTuneApplied() const noexcept
    {
        return juce::isPositiveAndBelow (selectedCandidate, 3)
             && compareFineTuneAppliedByCandidate[(size_t) selectedCandidate];
    }
    VoiceParameters getCurrentVoiceParameters() const { return readParams(); }
    VoiceParameters getMainVoiceParameters() const
    {
        VoiceParameters p;
        if (auto main = editingMain.load()) p = *main;
        else p = readParams ({}, false);

        // Tempo/sync settings are global even while a secondary instance is open
        // in the editor. Pull the live clock metadata from APVTS before matching.
        const auto live = readParams ({}, false);
        p.inheritTempoFrom (live);
        p.layers.fill (nullptr);
        p.mainLayerGain = 1.0f;
        return p;
    }

    bool savePreset (const juce::File&);
    bool validateReleaseState (juce::String* reason = nullptr) const;
    void loadFactoryPreset (int index);
    void randomizePreset();
    bool applyDirectedVariation (VariationDirection direction, float intensity, int64 seed);
    void captureMagicOrigin();
    bool restoreMagicOrigin();
    bool hasMagicOrigin() const noexcept { return magicOriginSnapshot.isValid(); }
    juce::String getPresetName() const { return apvts.state.getProperty ("patchName", "Custom patch").toString(); }
    bool loadPreset (const juce::File&);
    bool exportPreviewWav (const juce::File&, float seconds = 2.5f) const;

    void noteOnFromEditor (int midiNote, float velocity);
    void noteOffFromEditor (int midiNote, float velocity = 0.0f);
    void allEditorNotesOff();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    SynthEngine engine;
    DspRouting::AtomicPlan routingPlanPublisher;
    // True whole-instrument bus: runs once after main + all layer instances are combined.
    ModuleRack globalModuleRack;
    juce::MidiBuffer renderMidi;
    juce::MidiBuffer editorMidi;
    juce::CriticalSection editorMidiLock;
    std::array<std::atomic<std::shared_ptr<const VoiceParameters>>, VoiceParameters::extraLayerCount> savedLayers;
    ReferenceSamplePlayer referencePlayer;
    juce::AudioBuffer<float> referenceScratch;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> referenceLatencyDelay { 512 };
    juce::File loadedReferenceFile;
    std::atomic<int> referenceAuditionMode { (int) ReferenceAuditionMode::synthOnly };
    std::atomic<float> referenceAuditionLevel { 0.70f };
    std::atomic<int> referenceBaseMidiNote { 60 };
    std::atomic<float> analysisStartSeconds { 0.0f };
    std::atomic<float> analysisEndSeconds { -1.0f };
    std::atomic<float> analysisSourceDuration { 0.0f };
    std::atomic<bool> referencePitchLocked { false };
    std::atomic<float> effectiveBpm { 120.0f };
    mutable juce::CriticalSection midiMappingLock;
    std::vector<MidiMapping> midiMappings;
    juce::String midiLearnParameter;
    std::atomic<bool> midiLearning { false };
    std::atomic<float> outputPeakLeft { 0.0f };
    std::atomic<float> outputPeakRight { 0.0f };
    // Transient Compare correction state. It intentionally stays out of APVTS/session
    // automation until KEEP explicitly promotes the adjusted voice into the working patch.
    std::array<CompareFineTune::Values, 3> compareFineTuneValuesByCandidate {};
    std::array<CompareFineTune::Values, 3> compareFineTuneMeasuredValuesByCandidate {};
    std::array<std::optional<MatchResult>, 3> compareFineTuneMeasuredByCandidate {};
    std::array<bool, 3> compareFineTuneAppliedByCandidate {};
    bool compareFineTunePending = false;
    int detectedReferenceMidiNote = 60;
    float detectedReferenceHz = 0.0f;
    float detectedReferencePitchConfidence = 0.0f;

    std::atomic<int> editingLayer { -1 };
    std::atomic<std::shared_ptr<const VoiceParameters>> editingMain;
    juce::ValueTree editingMainSnapshot;
    juce::ValueTree magicOriginSnapshot;
    juce::ValueTree snapshotCurrent() const;
    juce::ValueTree canonicalState();
    void applyEditingSnapshot (const juce::ValueTree&);
    VoiceParameters readParams (const juce::ValueTree& snapshot = {}, bool routed = true) const;
    void restoreLayers();
    void applyPresetParameters (const VoiceParameters&, const juce::String& name);
    void applyGeneratedRack (const MatchResult& mainResult, int selectedBankIndex);
    void updateCandidatePreview (const MatchResult&);
    void clearCompareFineTuneState() noexcept;
    void invalidateMatchesAfterReferencePitchChange();
    void rebuildRoutingPlanFromState();
    void delayReferenceForLatency (juce::AudioBuffer<float>&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroMatchSynthAudioProcessor)
};
