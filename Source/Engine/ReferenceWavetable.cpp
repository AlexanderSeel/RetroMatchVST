#include "ReferenceWavetable.h"
#include <cmath>

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

std::shared_ptr<ReferenceWavetableData> ReferenceWavetableExtractor::extract (const juce::File& file, float expectedFundamentalHz)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader || reader->lengthInSamples < 128) return {};
    const auto sr = reader->sampleRate;
    float f0 = expectedFundamentalHz;
    if (f0 < 25.0f || f0 > 5000.0f) f0 = 220.0f;
    const int period = juce::jlimit (8, 8192, (int) std::round (sr / f0));
    if (reader->lengthInSamples < period * 2) return {};

    const int readSamples = (int) juce::jmin<int64> (reader->lengthInSamples, (int64) sr * 6);
    juce::AudioBuffer<float> audio ((int) juce::jmax ((unsigned int) 1, reader->numChannels), readSamples);
    reader->read (&audio, 0, readSamples, 0, true, true);
    juce::AudioBuffer<float> mono (1, readSamples); mono.clear();
    for (int c = 0; c < audio.getNumChannels(); ++c) mono.addFrom (0, 0, audio, c, 0, readSamples, 1.0f / audio.getNumChannels());

    auto result = std::make_shared<ReferenceWavetableData>(); result->fundamentalHz = f0;
    const int margin = period;
    for (int frame = 0; frame < ReferenceWavetableData::frameCount; ++frame)
    {
        const float t = (frame + 0.5f) / ReferenceWavetableData::frameCount;
        int centre = margin + (int) (t * juce::jmax (1, readSamples - 2 * margin));
        int start = juce::jlimit (0, readSamples - period - 1, centre - period / 2);
        // Nudge toward a positive-going zero crossing for phase stability.
        const float* x = mono.getReadPointer (0);
        int best = start;
        for (int n = juce::jmax (1, start - period / 3); n < juce::jmin (readSamples - period - 1, start + period / 3); ++n)
            if (x[n - 1] <= 0.0f && x[n] > 0.0f) { best = n; break; }
        start = best;

        float peak = 1.0e-6f;
        for (int i = 0; i < ReferenceWavetableData::tableSize; ++i)
        {
            const double src = start + (i / (double) ReferenceWavetableData::tableSize) * period;
            const int a = juce::jlimit (0, readSamples - 1, (int) std::floor (src));
            const int b = juce::jmin (readSamples - 1, a + 1);
            const float m = (float) (src - a);
            float v = juce::jmap (m, x[a], x[b]);
            // Remove a linear endpoint trend to reduce clicks when cycling.
            const float edge = juce::jmap (i / (float) (ReferenceWavetableData::tableSize - 1), x[start], x[juce::jmin (readSamples - 1, start + period)]);
            v -= edge;
            result->frames[(size_t) frame][(size_t) i] = v;
            peak = juce::jmax (peak, std::abs (v));
        }
        for (auto& v : result->frames[(size_t) frame]) v = juce::jlimit (-1.0f, 1.0f, v / peak * 0.9f);
    }
    result->valid = true;
    return result;
}
