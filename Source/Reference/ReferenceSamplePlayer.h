#pragma once
#include <JuceHeader.h>
#include <atomic>

// Polyphonic playback of the loaded reference sample for A/B auditioning.
// The sample is mapped across MIDI notes using the detected/manual root note.
class ReferenceSamplePlayer
{
public:
    ReferenceSamplePlayer();

    void prepare (double sampleRate);
    bool load (const juce::File& file, int rootMidiNote);
    bool setRootMidiNote (int rootMidiNote);
    void clear();

    void render (juce::AudioBuffer<float>& audio, const juce::MidiBuffer& midi, int startSample, int numSamples);
    void noteOnFromUi (int midiNote, float velocity);
    void noteOffFromUi (int midiNote, float velocity = 0.0f);
    void allNotesOff();

    bool previewRegion (const juce::File& file, int rootMidiNote, double startSeconds, double endSeconds,
                        bool normalize, float fadeInSeconds, float fadeOutSeconds);
    void stopPreview();
    bool isPreviewing() const noexcept { return previewing.load(); }
    static bool writeProcessedRegion (const juce::File& source, const juce::File& destination,
                                      double startSeconds, double endSeconds, bool normalize,
                                      float fadeInSeconds, float fadeOutSeconds);

    bool hasSample() const noexcept { return loaded.load(); }
    int getRootMidiNote() const noexcept { return rootNote.load(); }
    const juce::File& getSourceFile() const noexcept { return sourceFile; }

private:
    juce::Synthesiser synth, previewSynth;
    juce::AudioFormatManager formats;
    juce::File sourceFile;
    std::atomic<int> rootNote { 60 };
    std::atomic<bool> loaded { false }, previewing { false };
    double playbackSampleRate = 44100.0;

    bool rebuildSound();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceSamplePlayer)
};
