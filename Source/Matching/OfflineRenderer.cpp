#include "OfflineRenderer.h"
#include <cmath>

juce::AudioBuffer<float> OfflineRenderer::renderPatch (const VoiceParameters& params,
                                                        double sampleRate,
                                                        float durationSeconds,
                                                        float targetFundamentalHz,
                                                        int blockSize)
{
    const float duration = juce::jlimit (0.45f, 4.0f, durationSeconds);
    const int totalSamples = juce::jmax (1, (int) std::ceil (duration * sampleRate));
    juce::AudioBuffer<float> out (2, totalSamples);
    out.clear();

    SynthEngine engine;
    engine.prepare (sampleRate, blockSize, 2);
    engine.setParameters (params);

    float hz = targetFundamentalHz;
    if (hz < 25.0f || hz > 5000.0f) hz = 261.6256f;
    const int midiNote = juce::jlimit (0, 127, (int) std::round (69.0 + 12.0 * std::log2 (hz / 440.0)));

    const float gateSeconds = juce::jlimit (0.15f, duration * 0.85f,
                                            duration - juce::jmin (duration * 0.35f, params.release * 0.85f + 0.05f));
    const int noteOffSample = juce::jlimit (1, totalSamples - 1, (int) (gateSeconds * sampleRate));

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int count = juce::jmin (blockSize, totalSamples - start);
        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), out.getNumChannels(), start, count);
        juce::MidiBuffer midi;
        if (start == 0) midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, (juce::uint8) 108), 0);
        if (noteOffSample >= start && noteOffSample < start + count)
            midi.addEvent (juce::MidiMessage::noteOff (1, midiNote), noteOffSample - start);
        engine.render (block, midi);
    }
    return out;
}
