#pragma once
#include <JuceHeader.h>
#include "Engine/SynthEngine.h"
#include "Analysis/SampleAnalyzer.h"
#include "Matching/SoundMatcher.h"
#include "Matching/OfflineRenderer.h"

class RetroMatchSynthAudioProcessor : public juce::AudioProcessor
{
public:
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
    std::array<MatchResult, 3> candidateBank {};
    int selectedCandidate = 0;

    bool loadReferenceSample (const juce::File&);
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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    SynthEngine engine;
    VoiceParameters readParams() const;
    void updateCandidatePreview (const MatchResult&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroMatchSynthAudioProcessor)
};
