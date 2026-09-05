#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>

struct ReferenceWavetableData
{
    static constexpr int frameCount = 5;
    static constexpr int tableSize = 2048;
    std::array<std::array<float, tableSize>, frameCount> frames {};
    float fundamentalHz = 0.0f;
    bool valid = false;

    float sample (double phase, float position) const;
    juce::String toBase64() const;
    static std::shared_ptr<ReferenceWavetableData> fromBase64 (const juce::String&);
};

class ReferenceWavetableExtractor
{
public:
    static std::shared_ptr<ReferenceWavetableData> extract (const juce::File& file, float expectedFundamentalHz);

    // Imports common single-cycle and multi-frame wavetable files into RetroMatch's
    // immutable five-frame x 2048 internal representation. sourceFrameSize == 0
    // uses conservative auto-detection; callers may explicitly select 256/512/
    // 1024/2048/4096 for ambiguous files whose total length is divisible by more
    // than one common cycle size.
    static std::shared_ptr<ReferenceWavetableData> importSet (const juce::File& file,
                                                              int sourceFrameSize = 0,
                                                              juce::String* description = nullptr);
    static std::shared_ptr<ReferenceWavetableData> importSetFromBuffer (const juce::AudioBuffer<float>& audio,
                                                                        double sampleRate,
                                                                        int sourceFrameSize = 0,
                                                                        juce::String* description = nullptr);
};
