#pragma once
#include <JuceHeader.h>
#include "Engine/SynthEngine.h"
#include "Analysis/SampleAnalyzer.h"
#include "Matching/SoundMatcher.h"
#include "Matching/OfflineRenderer.h"
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
    bool setReferenceAnalysisRegion (float startSeconds, float endSeconds);
    MelodyClip getMelodyClip() const { return MelodyClip::fromState (apvts.state.getChildWithName ("MELODY")); }
    void setMelodyClip (const MelodyClip& clip)
    {
        melodyTransport.stop();
        auto previous = apvts.state.getChildWithName ("MELODY");
        if (previous.isValid()) apvts.state.removeChild (previous, nullptr);
        apvts.state.appendChild (clip.toState(), nullptr);
    }
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

    // Smoothed post-mix peaks for editor meters. The audio thread only performs
    // relaxed atomic stores; the UI thread reads these values without locks.
    float getOutputPeakLeft() const noexcept { return outputPeakLeft.load (std::memory_order_relaxed); }
    float getOutputPeakRight() const noexcept { return outputPeakRight.load (std::memory_order_relaxed); }

    MatchResult fitReference();
    MatchResult refineReference (SoundMatcher::ProgressCallback progress = {}, SoundMatcher::CancelCallback cancel = {});
    void applyMatchResult (const MatchResult&);
    std::array<MatchResult, 3> buildCandidateBank();
    bool selectCandidate (int index);
    void morphCandidates (int a, int b, float amount);
    VoiceParameters getCurrentVoiceParameters() const { return readParams(); }

    bool savePreset (const juce::File&);
    bool loadPreset (const juce::File&);
    bool exportPreviewWav (const juce::File&, float seconds = 2.5f) const;

    // Optional UI audition keyboard. It can audition the synth, the loaded
    // reference sample transposed from its base note, or both together.
    void noteOnFromEditor (int midiNote, float velocity);
    void noteOffFromEditor (int midiNote, float velocity = 0.0f);
    void allEditorNotesOff();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    SynthEngine engine;
    juce::MidiBuffer renderMidi;
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
    mutable juce::CriticalSection midiMappingLock;
    std::vector<MidiMapping> midiMappings;
    juce::String midiLearnParameter;
    std::atomic<bool> midiLearning { false };
    std::atomic<float> outputPeakLeft { 0.0f };
    std::atomic<float> outputPeakRight { 0.0f };
    int detectedReferenceMidiNote = 60;
    float detectedReferenceHz = 0.0f;
    float detectedReferencePitchConfidence = 0.0f;

    VoiceParameters readParams() const;
    void updateCandidatePreview (const MatchResult&);
    void invalidateMatchesAfterReferencePitchChange();
    void delayReferenceForLatency (juce::AudioBuffer<float>&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroMatchSynthAudioProcessor)
};
