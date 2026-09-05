#include "ReferenceWavetable.h"
#include "../Analysis/SampleAnalyzer.h"
#include <cmath>

namespace
{
int chooseSourceFrameSize (int totalSamples, int requested)
{
    if (totalSamples < 8) return 0;
    if (requested > 0) return requested <= totalSamples ? requested : 0;

    // 2048 is the de-facto default for modern wavetable sets. Explicit UI
    // selection handles ambiguous totals (for example 8192 can be 4x2048 or 8x1024).
    constexpr std::array<int, 5> candidates {{ 2048, 1024, 4096, 512, 256 }};
    for (const auto candidate : candidates)
        if (totalSamples >= candidate && totalSamples % candidate == 0)
            return candidate;

    // Short non-standard single cycles are still useful and are resampled to 2048.
    if (totalSamples <= 8192) return totalSamples;
    return totalSamples >= 2048 ? 2048 : totalSamples;
}

float cyclicFrameSample (const float* samples, int frameStart, int frameSize, double phase)
{
    if (samples == nullptr || frameSize <= 0) return 0.0f;
    phase -= std::floor (phase);
    const double position = phase * frameSize;
    const int i0 = ((int) std::floor (position)) % frameSize;
    const int i1 = (i0 + 1) % frameSize;
    const float fraction = (float) (position - std::floor (position));
    return juce::jmap (fraction, samples[frameStart + i0], samples[frameStart + i1]);
}
}

float ReferenceWavetableData::sample (double phase, float position) const
{
    if (! valid) return 0.0f;
    phase -= std::floor (phase);
    const float fp = juce::jlimit (0.0f, 1.0f, position) * (frameCount - 1);
    const int f0 = juce::jlimit (0, frameCount - 1, (int) std::floor (fp));
    const int f1 = juce::jmin (frameCount - 1, f0 + 1);
    const float fm = fp - f0;
    const double tp = phase * tableSize;
    const int i0 = ((int) tp) % tableSize, i1 = (i0 + 1) % tableSize;
    const float im = (float) (tp - std::floor (tp));
    auto at = [&] (int f) { return juce::jmap (im, frames[(size_t) f][(size_t) i0], frames[(size_t) f][(size_t) i1]); };
    return juce::jmap (fm, at (f0), at (f1));
}

juce::String ReferenceWavetableData::toBase64() const
{
    juce::MemoryOutputStream out;
    out.writeInt (0x524d5754); out.writeInt (1); out.writeFloat (fundamentalHz); out.writeBool (valid);
    for (const auto& frame : frames) for (float v : frame) out.writeFloat (v);
    return out.getMemoryBlock().toBase64Encoding();
}

std::shared_ptr<ReferenceWavetableData> ReferenceWavetableData::fromBase64 (const juce::String& text)
{
    juce::MemoryBlock block;
    if (! block.fromBase64Encoding (text) || block.getSize() < 16) return {};
    juce::MemoryInputStream in (block, false);
    if (in.readInt() != 0x524d5754 || in.readInt() != 1) return {};
    auto d = std::make_shared<ReferenceWavetableData>();
    d->fundamentalHz = in.readFloat(); d->valid = in.readBool();
    for (auto& frame : d->frames) for (auto& v : frame) { if (in.isExhausted()) return {}; v = in.readFloat(); }
    return d;
}

std::shared_ptr<ReferenceWavetableData> ReferenceWavetableExtractor::extract (const juce::File& file, float expectedFundamentalHz,
                                                                                double startSeconds, double endSeconds)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader || reader->lengthInSamples < 128 || ! std::isfinite (startSeconds)
        || ! std::isfinite (endSeconds) || reader->sampleRate <= 0) return {};
    const auto sr = reader->sampleRate;
    float f0 = expectedFundamentalHz;
    if (! std::isfinite (f0) || f0 < 25.0f || f0 > 5000.0f) f0 = 220.0f;
    const int period = juce::jlimit (8, 8192, (int) std::round (sr / f0));
    if (reader->lengthInSamples < period * 2) return {};

    const double duration = (double) reader->lengthInSamples / sr;
    const double startSec = juce::jlimit (0.0, duration, startSeconds);
    const double endSec = endSeconds < 0 ? duration : juce::jlimit (0.0, duration, endSeconds);
    if (endSec <= startSec) return {};
    const int64 startSample = (int64) std::llround (startSec * sr);
    const int64 endSample = juce::jmin (reader->lengthInSamples, (int64) std::llround (endSec * sr));
    const int readSamples = (int) juce::jmin<int64> (endSample - startSample, (int64) sr * 6);
    if (readSamples < period * 2 + 2) return {};
    juce::AudioBuffer<float> audio ((int) juce::jmax ((unsigned int) 1, reader->numChannels), readSamples);
    if (! reader->read (&audio, 0, readSamples, startSample, true, true)) return {};
    juce::AudioBuffer<float> mono (1, readSamples); mono.clear();
    for (int c = 0; c < audio.getNumChannels(); ++c) mono.addFrom (0, 0, audio, c, 0, readSamples, 1.0f / audio.getNumChannels());
    if (mono.getMagnitude (0, readSamples) < 1.0e-6f) return {};

    auto result = std::make_shared<ReferenceWavetableData>(); result->fundamentalHz = f0;
    const int margin = period;
    const float* x = mono.getReadPointer (0);
    const float audibleThreshold = mono.getMagnitude (0, readSamples) * 0.01f;
    int activeStart = 0;
    while (activeStart < readSamples - 2 * period && std::abs (x[activeStart]) < audibleThreshold) ++activeStart;
    for (int frame = 0; frame < ReferenceWavetableData::frameCount; ++frame)
    {
        const float t = (frame + 0.5f) / ReferenceWavetableData::frameCount;
        int centre = activeStart + margin + (int) (t * juce::jmax (1, readSamples - activeStart - 2 * margin));
        int start = juce::jlimit (0, readSamples - period - 1, centre - period / 2);
        // Nudge toward a positive-going zero crossing for phase stability.
        int best = start;
        int bestDistance = period + 1;
        for (int n = juce::jmax (1, start - period); n < juce::jmin (readSamples - period - 1, start + period); ++n)
            if (x[n - 1] <= 0.0f && x[n] > 0.0f && std::abs (n - start) < bestDistance)
            { best = n; bestDistance = std::abs (n - start); }
        start = best;
        double cycleStart = start;
        if (start > 0 && x[start - 1] <= 0.0f && x[start] > 0.0f)
            cycleStart = start - 1 + (double) -x[start - 1] / (x[start] - x[start - 1]);
        const double cycleLength = sr / f0;
        auto interpolate = [&] (double position)
        {
            position = juce::jlimit (0.0, (double) readSamples - 1, position);
            const int a = (int) position;
            return juce::jmap ((float) (position - a), x[a], x[juce::jmin (readSamples - 1, a + 1)]);
        };

        float peak = 1.0e-6f;
        for (int i = 0; i < ReferenceWavetableData::tableSize; ++i)
        {
            const double src = cycleStart + (i / (double) ReferenceWavetableData::tableSize) * cycleLength;
            const int a = juce::jlimit (0, readSamples - 1, (int) std::floor (src));
            const int b = juce::jmin (readSamples - 1, a + 1);
            const float m = (float) (src - a);
            float v = juce::jmap (m, x[a], x[b]);
            // Remove a linear endpoint trend to reduce clicks when cycling.
            const float edge = juce::jmap (i / (float) ReferenceWavetableData::tableSize,
                                          interpolate (cycleStart), interpolate (cycleStart + cycleLength));
            v -= edge;
            result->frames[(size_t) frame][(size_t) i] = v;
            peak = juce::jmax (peak, std::abs (v));
        }
        for (auto& v : result->frames[(size_t) frame]) v = juce::jlimit (-1.0f, 1.0f, v / peak * 0.9f);
    }
    result->valid = true;
    return result;
}

std::shared_ptr<ReferenceWavetableData> ReferenceWavetableExtractor::chop (const juce::File& file, double start, double end, int slices)
{
    if (! std::isfinite (start) || ! std::isfinite (end) || start < 0 || end <= start || slices < 2 || slices > 5) return {};
    std::array<std::shared_ptr<ReferenceWavetableData>, 5> tables;
    for (int i = 0; i < slices; ++i)
    {
        const double s = start + (end - start) * i / slices;
        const double e = start + (end - start) * (i + 1) / slices;
        auto features = SampleAnalyzer::analyzeFile (file, 0, s, e);
        if (! features) return {};
        tables[(size_t) i] = extract (file, features->fundamentalHz, s, e);
        if (! tables[(size_t) i]) return {};
    }
    auto result = std::make_shared<ReferenceWavetableData>();
    result->fundamentalHz = tables[0]->fundamentalHz; result->valid = true;
    for (int i = 0; i < ReferenceWavetableData::frameCount; ++i)
        result->frames[(size_t) i] = tables[(size_t) std::lround (i * (slices - 1) / 4.0)]->frames[2];
    return result;
}

std::shared_ptr<ReferenceWavetableData> ReferenceWavetableExtractor::importSet (const juce::File& file,
                                                                                int sourceFrameSize,
                                                                                juce::String* description)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader || reader->lengthInSamples < 8) return {};

    // 512 source frames at the largest supported cycle size is already far beyond
    // what the five-frame internal representation needs while keeping imports bounded.
    constexpr int maxImportedSamples = 4096 * 512;
    const int readSamples = (int) juce::jmin<int64> (reader->lengthInSamples, maxImportedSamples);
    juce::AudioBuffer<float> audio ((int) juce::jmax ((unsigned int) 1, reader->numChannels), readSamples);
    if (! reader->read (&audio, 0, readSamples, 0, true, true)) return {};
    return importSetFromBuffer (audio, reader->sampleRate, sourceFrameSize, description);
}

std::shared_ptr<ReferenceWavetableData> ReferenceWavetableExtractor::importSetFromBuffer (const juce::AudioBuffer<float>& audio,
                                                                                          double sampleRate,
                                                                                          int sourceFrameSize,
                                                                                          juce::String* description)
{
    if (audio.getNumChannels() <= 0 || audio.getNumSamples() < 8) return {};

    juce::AudioBuffer<float> mono (1, audio.getNumSamples());
    mono.clear();
    const float channelGain = 1.0f / (float) audio.getNumChannels();
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        mono.addFrom (0, 0, audio, ch, 0, audio.getNumSamples(), channelGain);

    const int frameSize = chooseSourceFrameSize (mono.getNumSamples(), sourceFrameSize);
    if (frameSize < 8 || frameSize > mono.getNumSamples()) return {};
    const int availableFrames = mono.getNumSamples() / frameSize;
    if (availableFrames <= 0) return {};

    const int sourceFrames = juce::jmin (512, availableFrames);
    const float* samples = mono.getReadPointer (0);
    auto result = std::make_shared<ReferenceWavetableData>();
    result->fundamentalHz = sampleRate > 0.0 ? (float) (sampleRate / frameSize) : 0.0f;

    float globalPeak = 1.0e-6f;
    for (int targetFrame = 0; targetFrame < ReferenceWavetableData::frameCount; ++targetFrame)
    {
        const float sourcePosition = sourceFrames <= 1 ? 0.0f
            : (float) targetFrame / (float) (ReferenceWavetableData::frameCount - 1) * (float) (sourceFrames - 1);
        const int source0 = juce::jlimit (0, sourceFrames - 1, (int) std::floor (sourcePosition));
        const int source1 = juce::jmin (sourceFrames - 1, source0 + 1);
        const float sourceMix = sourcePosition - (float) source0;

        float mean = 0.0f;
        for (int i = 0; i < ReferenceWavetableData::tableSize; ++i)
        {
            const double phase = i / (double) ReferenceWavetableData::tableSize;
            const float a = cyclicFrameSample (samples, source0 * frameSize, frameSize, phase);
            const float b = cyclicFrameSample (samples, source1 * frameSize, frameSize, phase);
            const float value = juce::jmap (sourceMix, a, b);
            result->frames[(size_t) targetFrame][(size_t) i] = value;
            mean += value;
        }
        mean /= (float) ReferenceWavetableData::tableSize;
        for (auto& value : result->frames[(size_t) targetFrame])
        {
            value -= mean;
            globalPeak = juce::jmax (globalPeak, std::abs (value));
        }
    }

    const float normalise = 0.95f / globalPeak;
    for (auto& frame : result->frames)
        for (auto& value : frame)
            value = juce::jlimit (-1.0f, 1.0f, value * normalise);

    result->valid = true;
    if (description != nullptr)
    {
        *description = juce::String (availableFrames) + " source frame" + (availableFrames == 1 ? "" : "s")
                     + " x " + juce::String (frameSize) + " samples -> 5 x 2048";
        if (availableFrames > sourceFrames) *description += " (first 512 frames used)";
    }
    return result;
}
