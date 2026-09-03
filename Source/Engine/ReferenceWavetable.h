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
};
