#include "ReferenceSamplePlayer.h"

ReferenceSamplePlayer::ReferenceSamplePlayer()
{
    formats.registerBasicFormats();
    for (int i = 0; i < 8; ++i)
        synth.addVoice (new juce::SamplerVoice());
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
}

void ReferenceSamplePlayer::prepare (double sampleRate)
{
    if (sampleRate > 1000.0)
        playbackSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
}

bool ReferenceSamplePlayer::load (const juce::File& file, int rootMidiNote)
{
    sourceFile = file;
    rootNote.store (juce::jlimit (0, 127, rootMidiNote));
    return rebuildSound();
}

bool ReferenceSamplePlayer::setRootMidiNote (int rootMidiNote)
{
    rootNote.store (juce::jlimit (0, 127, rootMidiNote));
    if (! sourceFile.existsAsFile()) return false;
    allNotesOff();
    return rebuildSound();
}

void ReferenceSamplePlayer::clear()
{
    allNotesOff();
    synth.clearSounds();
    sourceFile = juce::File();
    loaded.store (false);
}

bool ReferenceSamplePlayer::rebuildSound()
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (sourceFile));
    if (reader == nullptr)
    {
        synth.clearSounds();
        loaded.store (false);
        return false;
    }

    juce::BigInteger notes;
    notes.setRange (0, 128, true);

    auto sound = std::make_unique<juce::SamplerSound> (
        sourceFile.getFileNameWithoutExtension(), *reader, notes, rootNote.load(),
        0.002, 0.06, 12.0);

    synth.clearSounds();
    synth.addSound (sound.release());
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
    loaded.store (true);
    return true;
}

void ReferenceSamplePlayer::render (juce::AudioBuffer<float>& audio,
                                    const juce::MidiBuffer& midi,
                                    int startSample,
                                    int numSamples)
{
    if (! loaded.load() || numSamples <= 0) return;
    synth.renderNextBlock (audio, midi, startSample, numSamples);
}

void ReferenceSamplePlayer::noteOnFromUi (int midiNote, float velocity)
{
    if (! loaded.load()) return;
    synth.noteOn (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity));
}

void ReferenceSamplePlayer::noteOffFromUi (int midiNote, float velocity)
{
    synth.noteOff (1, juce::jlimit (0, 127, midiNote), juce::jlimit (0.0f, 1.0f, velocity), true);
}

void ReferenceSamplePlayer::allNotesOff()
{
    synth.allNotesOff (0, true);
}
