#include "MelodyAnalyzer.h"
#include <algorithm>
#include <cmath>

juce::MidiMessageSequence MelodyClip::sequenceInSeconds() const
{
    juce::MidiMessageSequence result;
    for (const auto& note : notes)
    {
        auto on = juce::MidiMessage::noteOn (16, note.pitch, note.velocity);
        auto off = juce::MidiMessage::noteOff (16, note.pitch);
        on.setTimeStamp (note.start); off.setTimeStamp (note.start + note.duration);
        result.addEvent (on); result.addEvent (off);
    }
    result.sort(); result.updateMatchedPairs();
    return result;
}

bool MelodyClip::writeMidi (const juce::File& file) const
{
    if (notes.empty()) return false;
    const double tempo = juce::jlimit (30.0, 300.0, bpm);
    auto track = sequenceInSeconds();
    for (int i = 0; i < track.getNumEvents(); ++i)
    {
        auto& message = track.getEventPointer (i)->message;
        message.setChannel (1);
        message.setTimeStamp (message.getTimeStamp() * 960.0 * tempo / 60.0);
    }
    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / tempo)));
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "RetroMatch - " + sourceName));
    track.sort(); track.updateMatchedPairs();
    juce::MidiFile midi; midi.setTicksPerQuarterNote (960); midi.addTrack (track);
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream output (temp.getFile());
        if (! output.openedOk() || ! midi.writeTo (output)) return false;
        output.flush();
        if (output.getStatus().failed()) return false;
    }
    return temp.overwriteTargetFileWithTemporary();
}

juce::ValueTree MelodyClip::toState() const
{
    juce::ValueTree state ("MELODY");
    state.setProperty ("bpm", bpm, nullptr); state.setProperty ("duration", duration, nullptr);
    state.setProperty ("source", sourceName, nullptr); state.setProperty ("layered", layered, nullptr);
    state.setProperty ("truncated", truncated, nullptr);
    for (const auto& note : notes)
    {
        juce::ValueTree n ("NOTE");
        n.setProperty ("pitch", note.pitch, nullptr); n.setProperty ("start", note.start, nullptr);
        n.setProperty ("duration", note.duration, nullptr); n.setProperty ("velocity", note.velocity, nullptr);
        n.setProperty ("confidence", note.confidence, nullptr); state.appendChild (n, nullptr);
    }
    return state;
}

MelodyClip MelodyClip::fromState (const juce::ValueTree& state)
{
    MelodyClip clip;
    if (! state.hasType ("MELODY")) return clip;
    auto bounded = [] (double value, double lo, double hi, double fallback)
    { return std::isfinite (value) ? juce::jlimit (lo, hi, value) : fallback; };
    clip.bpm = bounded ((double) state.getProperty ("bpm", 120.0), 30.0, 300.0, 120.0);
    clip.duration = bounded ((double) state.getProperty ("duration"), 0.0, 60.0, 0.0);
    clip.sourceName = state.getProperty ("source").toString().substring (0, 256);
    clip.layered = state.getProperty ("layered"); clip.truncated = state.getProperty ("truncated");
    for (const auto& n : state)
    {
        if (! n.hasType ("NOTE") || (int) clip.notes.size() >= maxNotes) continue;
        TranscribedNote note;
        note.pitch = juce::jlimit (0, 127, (int) n.getProperty ("pitch", 60));
        note.start = bounded ((double) n.getProperty ("start"), 0.0, 59.99, 0.0);
        note.duration = bounded ((double) n.getProperty ("duration"), 0.01, 60.0 - note.start, 0.1);
        note.velocity = (float) bounded ((double) n.getProperty ("velocity"), 0.05, 1.0, 0.8);
        note.confidence = (float) bounded ((double) n.getProperty ("confidence"), 0.0, 1.0, 0.0);
        clip.notes.push_back (note); clip.duration = std::max (clip.duration, note.start + note.duration);
    }
    std::sort (clip.notes.begin(), clip.notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    return clip;
}

MelodyClip MelodyAnalyzer::analyzeFile (const juce::File& file, bool layered, double bpm, Cancel cancel, Progress progress)
{
    return analyzeFile (file, layered, bpm, 0.0, -1.0, std::move (cancel), std::move (progress));
}

MelodyClip MelodyAnalyzer::analyzeFile (const juce::File& file, bool layered, double bpm,
                                        double startSeconds, double endSeconds, Cancel cancel, Progress progress)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || reader->sampleRate <= 0.0) return {};
    const double fileDuration = (double) reader->lengthInSamples / reader->sampleRate;
    const double start = juce::jlimit (0.0, fileDuration, startSeconds);
    const double end = endSeconds > start ? juce::jlimit (start + 1.0 / reader->sampleRate, fileDuration, endSeconds) : fileDuration;
    const int64 startSample = (int64) std::llround (start * reader->sampleRate);
    const int length = (int) std::min ((double) reader->lengthInSamples - startSample, reader->sampleRate * 60.0);
    juce::AudioBuffer<float> audio (juce::jlimit (1, 2, (int) reader->numChannels), length);
    if (! reader->read (&audio, 0, length, startSample, true, true)) return {};
    auto clip = analyzeBuffer (audio, reader->sampleRate, layered, bpm, cancel, progress);
    clip.sourceName = file.getFileName(); clip.truncated = clip.truncated || (end - start) > 60.0;
    return clip;
}

MelodyClip MelodyAnalyzer::analyzeBuffer (const juce::AudioBuffer<float>& audio, double sr,
                                         bool layered, double bpm, Cancel cancel, Progress progress)
{
    MelodyClip clip; clip.layered = layered; clip.bpm = std::isfinite (bpm) ? juce::jlimit (30.0, 300.0, bpm) : 120.0;
    if (! std::isfinite (sr) || sr < 8000.0 || audio.getNumChannels() == 0 || audio.getNumSamples() == 0) return clip;
    constexpr double analysisRate = 22050.0;
    constexpr int fftSize = 4096, hop = 220, firstNote = 28, lastNote = 96;
    const int count = (int) (std::min (60.0, audio.getNumSamples() / sr) * analysisRate);
    if (count < 64) return clip;
    clip.duration = count / analysisRate;
    clip.truncated = audio.getNumSamples() / sr > 60.0;
    std::vector<float> mono ((size_t) count);
    float peak = 0.0f;
    // Average each source interval when reducing sample rate; linear interpolation
    // when increasing it. All resampling and FFT work is on the analysis worker.
    for (int i = 0; i < count; ++i)
    {
        const double source = i * sr / analysisRate;
        const int a = juce::jlimit (0, audio.getNumSamples() - 1, (int) source);
        const int end = juce::jlimit (a + 1, audio.getNumSamples(), (int) ((i + 1) * sr / analysisRate));
        float value = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int j = a; j < end; ++j) value += audio.getSample (ch, j);
        mono[(size_t) i] = value / ((end - a) * audio.getNumChannels());
        if (sr < analysisRate)
        {
            float nextValue = 0.0f;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                nextValue += audio.getSample (ch, std::min (a + 1, audio.getNumSamples() - 1));
            mono[(size_t) i] = juce::jmap ((float) (source - a), mono[(size_t) i], nextValue / audio.getNumChannels());
        }
        peak = std::max (peak, std::abs (mono[(size_t) i]));
    }
    if (peak < 1.0e-6f) return clip;
    juce::dsp::FFT fft (12);
    juce::dsp::WindowingFunction<float> window (fftSize, juce::dsp::WindowingFunction<float>::hann, true);
    std::array<float, fftSize * 2> data {};
    std::array<float, fftSize / 2> residual {};
    struct Track { int hits = 0, misses = 0; double start = 0.0, last = 0.0; float strength = 0.0f, confidence = 0.0f; };
    std::array<Track, 128> tracks {};
    const double step = hop / analysisRate;
    auto finish = [&] (int pitch)
    {
        auto& t = tracks[(size_t) pitch];
        const double length = std::min (clip.duration, t.last + step) - t.start;
        if (t.hits >= 3 && length >= 0.065 && (int) clip.notes.size() >= MelodyClip::maxNotes) clip.truncated = true;
        if (t.hits >= 3 && length >= 0.065 && (int) clip.notes.size() < MelodyClip::maxNotes)
            clip.notes.push_back ({ pitch, t.start, length, juce::jlimit (0.15f, 1.0f, t.strength),
                                    juce::jlimit (0.0f, 1.0f, t.confidence / t.hits) });
        t = {};
    };
    for (int centre = 0; centre < count; centre += hop)
    {
        if (cancel && cancel()) return {};
        data.fill (0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            const int index = centre + i - fftSize / 2;
            if (index >= 0 && index < count) data[(size_t) i] = mono[(size_t) index] / peak;
        }
        window.multiplyWithWindowingTable (data.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (data.data());
        std::copy_n (data.begin(), residual.size(), residual.begin());
        float framePeak = 0.0f;
        double totalEnergy = 1.0e-12;
        for (int i = 4; i < fftSize / 2; ++i)
        { framePeak = std::max (framePeak, residual[(size_t) i]); totalEnergy += residual[(size_t) i] * residual[(size_t) i]; }
        double localEnergy = 0.0;
        for (int i = std::max (0, centre - hop); i < std::min (count, centre + hop); ++i)
            localEnergy += mono[(size_t) i] * mono[(size_t) i];
        const bool quiet = std::sqrt (localEnergy / (hop * 2)) < peak * 0.015;
        std::array<float, 128> detected {};
        float firstScore = 0.0f;
        auto magnitude = [&] (float hz)
        {
            const float bin = hz * fftSize / (float) analysisRate;
            if (bin < 2.0f || bin >= fftSize / 2 - 2) return 0.0f;
            const int index = (int) std::round (bin);
            return std::max ({ residual[(size_t) index], residual[(size_t) index - 1] * 0.8f,
                               residual[(size_t) index + 1] * 0.8f });
        };
        if (framePeak > 3.0f && ! quiet)
            for (int voice = 0; voice < (layered ? 4 : 1); ++voice)
            {
                float bestScore = 0.0f; int bestPitch = -1;
                for (int pitch = firstNote; pitch <= lastNote; ++pitch)
                {
                    const float hz = (float) juce::MidiMessage::getMidiNoteInHertz (pitch);
                    const float fundamental = magnitude (hz);
                    if (fundamental < framePeak * 0.14f) continue;
                    float score = fundamental;
                    for (int harmonic = 2; harmonic <= 5; ++harmonic)
                        score += magnitude (hz * harmonic) / (harmonic * 1.5f);
                    if (score > bestScore) { bestScore = score; bestPitch = pitch; }
                }
                if (voice == 0) firstScore = bestScore;
                if (bestPitch < 0 || bestScore < firstScore * 0.23f) break;
                const float hz = (float) juce::MidiMessage::getMidiNoteInHertz (bestPitch);
                double explainedEnergy = 0.0;
                for (int harmonic = 1; harmonic <= 16; ++harmonic)
                {
                    const int bin = (int) std::round (hz * harmonic * fftSize / analysisRate);
                    if (bin >= fftSize / 2 - 3) break;
                    for (int k = std::max (0, bin - 2); k <= bin + 2; ++k)
                    { explainedEnergy += residual[(size_t) k] * residual[(size_t) k]; residual[(size_t) k] = 0.0f; }
                }
                const float confidence = (float) std::sqrt (std::min (1.0, explainedEnergy / totalEnergy));
                if (confidence >= 0.32f) detected[(size_t) bestPitch] = confidence;
            }
        for (int pitch = firstNote; pitch <= lastNote; ++pitch)
        {
            auto& t = tracks[(size_t) pitch];
            if (detected[(size_t) pitch] > 0.0f)
            {
                if (t.hits == 0) t.start = centre / analysisRate;
                ++t.hits; t.misses = 0; t.last = centre / analysisRate;
                t.confidence += detected[(size_t) pitch];
                t.strength = std::max (t.strength, std::sqrt (framePeak / (fftSize * 0.45f)));
            }
            else if (t.hits > 0 && (t.misses += quiet ? 2 : 1) >= 4) finish (pitch);
        }
        if (progress && centre % (hop * 10) == 0) progress (centre / (float) count);
    }
    for (int pitch = firstNote; pitch <= lastNote; ++pitch) finish (pitch);
    std::sort (clip.notes.begin(), clip.notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    if (progress) progress (1.0f);
    return clip;
}
