#include "ReferenceSamplePlayer.h"
#include <cmath>

namespace
{
void processFilter (juce::AudioBuffer<float>& audio,
                    const juce::dsp::IIR::Coefficients<float>::Ptr& coefficients)
{
    if (coefficients == nullptr) return;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        juce::dsp::IIR::Filter<float> filter;
        filter.coefficients = coefficients;
        filter.reset();
        auto* samples = audio.getWritePointer (ch);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            samples[i] = filter.processSample (samples[i]);
    }
}

void applyTone (juce::AudioBuffer<float>& audio, double sampleRate,
                const ReferenceSamplePlayer::EditTone& tone)
{
    if (audio.getNumSamples() <= 0 || sampleRate <= 1000.0 || tone.isNeutral()) return;

    const float nyquistSafe = (float) juce::jmax (1000.0, sampleRate * 0.45);
    const float lowCut = juce::jlimit (10.0f, juce::jmin (1000.0f, nyquistSafe * 0.45f), tone.lowCutHz);
    const float highCut = juce::jlimit (juce::jmax (1200.0f, lowCut * 1.6f), nyquistSafe, tone.highCutHz);
    constexpr float q = 0.70710678f;

    if (lowCut > 20.01f)
        processFilter (audio, juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, lowCut, q));

    if (std::abs (tone.lowGainDb) > 0.001f)
        processFilter (audio, juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, juce::jmin (180.0f, highCut * 0.25f), q,
            juce::Decibels::decibelsToGain (tone.lowGainDb)));

    if (std::abs (tone.midGainDb) > 0.001f)
        processFilter (audio, juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, juce::jmin (1200.0f, highCut * 0.42f), 0.82f,
            juce::Decibels::decibelsToGain (tone.midGainDb)));

    if (std::abs (tone.highGainDb) > 0.001f)
        processFilter (audio, juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, juce::jmin (6500.0f, highCut * 0.72f), q,
            juce::Decibels::decibelsToGain (tone.highGainDb)));

    if (highCut < juce::jmin (19999.0f, nyquistSafe - 1.0f))
        processFilter (audio, juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, highCut, q));
}

bool readProcessedRegion (const juce::File& source, double startSeconds, double endSeconds,
                          bool normalize, float fadeInSeconds, float fadeOutSeconds,
                          const ReferenceSamplePlayer::EditTone& tone,
                          juce::AudioBuffer<float>& audio, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) return false;

    const double duration = (double) reader->lengthInSamples / reader->sampleRate;
    const double start = juce::jlimit (0.0, juce::jmax (0.0, duration - 1.0 / reader->sampleRate), startSeconds);
    const double end = juce::jlimit (start + 1.0 / reader->sampleRate, duration,
                                     endSeconds > start ? endSeconds : duration);
    const int64_t firstSample = (int64_t) std::llround (start * reader->sampleRate);
    const int64_t requested = (int64_t) std::llround ((end - start) * reader->sampleRate);
    const int64_t available = juce::jmax<int64_t> (0, reader->lengthInSamples - firstSample);
    const int64_t count64 = juce::jmin (requested, available);
    if (count64 <= 0 || count64 > (int64_t) std::numeric_limits<int>::max()) return false;

    const int count = (int) count64;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    audio.setSize (channels, count, false, false, true);
    if (! reader->read (&audio, 0, count, firstSample, true, true)) return false;
    sampleRate = reader->sampleRate;

    // Reference tone shaping is intentionally offline: the audio callback never
    // builds filters or reallocates buffers. Normalize after EQ so -1 dBFS refers
    // to the sound the user actually hears and exports.
    applyTone (audio, sampleRate, tone);

    float gain = 1.0f;
    if (normalize)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) peak = juce::jmax (peak, audio.getMagnitude (ch, 0, count));
        if (peak > 1.0e-7f) gain = juce::Decibels::decibelsToGain (-1.0f) / peak;
    }
    if (gain != 1.0f) audio.applyGain (gain);

    const int fadeInSamples = juce::jlimit (0, count / 2, (int) std::llround (juce::jmax (0.0f, fadeInSeconds) * sampleRate));
    const int fadeOutSamples = juce::jlimit (0, count / 2, (int) std::llround (juce::jmax (0.0f, fadeOutSeconds) * sampleRate));
    for (int ch = 0; ch < channels; ++ch)
    {
        if (fadeInSamples > 0) audio.applyGainRamp (ch, 0, fadeInSamples, 0.0f, 1.0f);
        if (fadeOutSamples > 0) audio.applyGainRamp (ch, count - fadeOutSamples, fadeOutSamples, 1.0f, 0.0f);
    }
    return true;
}
}

ReferenceSamplePlayer::ReferenceSamplePlayer()
{
    formats.registerBasicFormats();
    for (int i = 0; i < 8; ++i) synth.addVoice (new juce::SamplerVoice());
    previewSynth.addVoice (new juce::SamplerVoice());
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
    previewSynth.setCurrentPlaybackSampleRate (playbackSampleRate);
}

void ReferenceSamplePlayer::prepare (double sampleRate)
{
    if (sampleRate > 1000.0) playbackSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (playbackSampleRate);
    previewSynth.setCurrentPlaybackSampleRate (playbackSampleRate);
}

bool ReferenceSamplePlayer::load (const juce::File& file, int rootMidiNote)
{
    sourceFile = file;
    rootNote.store (juce::jlimit (0, 127, rootMidiNote));
    stopPreview();
    return rebuildSound();
}

bool ReferenceSamplePlayer::setRootMidiNote (int rootMidiNote)
{
    rootNote.store (juce::jlimit (0, 127, rootMidiNote));
    if (! sourceFile.existsAsFile()) return false;
    allNotesOff();
    stopPreview();
    return rebuildSound();
}

void ReferenceSamplePlayer::clear()
{
    allNotesOff();
    stopPreview();
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
    if (numSamples <= 0) return;
    if (loaded.load()) synth.renderNextBlock (audio, midi, startSample, numSamples);
    if (previewing.load())
    {
        juce::MidiBuffer noMidi;
        previewSynth.renderNextBlock (audio, noMidi, startSample, numSamples);
        bool anyActive = false;
        for (int i = 0; i < previewSynth.getNumVoices(); ++i)
            if (auto* voice = previewSynth.getVoice (i); voice != nullptr && voice->isVoiceActive()) { anyActive = true; break; }
        previewing.store (anyActive);
    }
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

bool ReferenceSamplePlayer::writeProcessedRegion (const juce::File& source, const juce::File& destination,
                                                  double startSeconds, double endSeconds, bool normalize,
                                                  float fadeInSeconds, float fadeOutSeconds)
{
    return writeProcessedRegion (source, destination, startSeconds, endSeconds, normalize,
                                 fadeInSeconds, fadeOutSeconds, EditTone {});
}

bool ReferenceSamplePlayer::writeProcessedRegion (const juce::File& source, const juce::File& destination,
                                                  double startSeconds, double endSeconds, bool normalize,
                                                  float fadeInSeconds, float fadeOutSeconds, const EditTone& tone)
{
    juce::AudioBuffer<float> audio;
    double sr = 0.0;
    if (! readProcessedRegion (source, startSeconds, endSeconds, normalize, fadeInSeconds, fadeOutSeconds,
                               tone, audio, sr)) return false;

    juce::TemporaryFile temp (destination);
    std::unique_ptr<juce::OutputStream> stream = temp.getFile().createOutputStream();
    if (! stream) return false;
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriter::Options {}
                             .withSampleRate (sr)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (! writer || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) return false;
    writer.reset();
    return temp.overwriteTargetFileWithTemporary();
}

bool ReferenceSamplePlayer::previewRegion (const juce::File& file, int rootMidiNote,
                                           double startSeconds, double endSeconds, bool normalize,
                                           float fadeInSeconds, float fadeOutSeconds)
{
    return previewRegion (file, rootMidiNote, startSeconds, endSeconds, normalize,
                          fadeInSeconds, fadeOutSeconds, EditTone {});
}

bool ReferenceSamplePlayer::previewRegion (const juce::File& file, int rootMidiNote,
                                           double startSeconds, double endSeconds, bool normalize,
                                           float fadeInSeconds, float fadeOutSeconds, const EditTone& tone)
{
    stopPreview();
    allNotesOff();
    const double previewEnd = juce::jmin (endSeconds, startSeconds + 180.0);
    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("RetroMatch-reference-preview", ".wav", false);
    if (! writeProcessedRegion (file, temp, startSeconds, previewEnd, normalize, fadeInSeconds, fadeOutSeconds, tone)) return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (temp));
    if (reader == nullptr) { temp.deleteFile(); return false; }
    juce::BigInteger notes;
    notes.setRange (0, 128, true);
    const double length = juce::jmin (180.0, juce::jmax (0.01, previewEnd - startSeconds));
    auto sound = std::make_unique<juce::SamplerSound> (
        "Reference preview", *reader, notes, juce::jlimit (0, 127, rootMidiNote), 0.001, 0.03, length + 0.5);
    previewSynth.clearSounds();
    previewSynth.addSound (sound.release());
    previewSynth.setCurrentPlaybackSampleRate (playbackSampleRate);
    temp.deleteFile();
    previewSynth.noteOn (1, juce::jlimit (0, 127, rootMidiNote), 1.0f);
    previewing.store (true);
    return true;
}

void ReferenceSamplePlayer::stopPreview()
{
    previewSynth.allNotesOff (0, false);
    previewSynth.clearSounds();
    previewing.store (false);
}