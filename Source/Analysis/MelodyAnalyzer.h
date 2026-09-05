#pragma once
#include <JuceHeader.h>
#include <functional>

struct TranscribedNote
{
    int pitch = 60;
    double start = 0.0, duration = 0.1;
    float velocity = 0.8f, confidence = 0.0f;
};

struct MelodyClip
{
    static constexpr int maxNotes = 4096;
    std::vector<TranscribedNote> notes;
    double duration = 0.0, bpm = 120.0;
    juce::String sourceName;
    bool layered = false, truncated = false;
    juce::MidiMessageSequence sequenceInSeconds() const;
    bool writeMidi (const juce::File&) const;
    juce::ValueTree toState() const;
    static MelodyClip fromState (const juce::ValueTree&);
};

class MelodyAnalyzer
{
public:
    using Cancel = std::function<bool()>;
    using Progress = std::function<void(float)>;
    static MelodyClip analyzeFile (const juce::File&, bool layered, double bpm,
                                   Cancel = {}, Progress = {});
    static MelodyClip analyzeFile (const juce::File&, bool layered, double bpm,
                                   double startSeconds, double endSeconds,
                                   Cancel = {}, Progress = {});
    static MelodyClip analyzeBuffer (const juce::AudioBuffer<float>&, double sampleRate,
                                     bool layered, double bpm, Cancel = {}, Progress = {});
};
