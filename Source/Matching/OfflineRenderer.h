#pragma once
#include <JuceHeader.h>
#include "../Engine/SynthEngine.h"

class OfflineRenderer
{
public:
    static juce::AudioBuffer<float> renderPatch (const VoiceParameters& params,
                                                  double sampleRate,
                                                  float durationSeconds,
                                                  float targetFundamentalHz,
                                                  int blockSize = 256);
};
