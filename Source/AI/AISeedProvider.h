#pragma once
#include <JuceHeader.h>
#include "AISettings.h"
#include "../Analysis/SampleAnalyzer.h"
#include "../Matching/SoundMatcher.h"

struct AIVariantBatch
{
    std::array<MatchResult, 3> candidates {};
    juce::String error;
    juce::String providerSummary;
    juce::String diagnostics;

    bool succeeded() const
    {
        return error.isEmpty() && candidates[0].confidence > 0.0f;
    }
};

class AISeedProvider
{
public:
    static AIVariantBatch generateVariants (const SoundFeatures& reference,
                                            const VoiceParameters& base,
                                            const MatchSettings& matchSettings,
                                            const AISettings& settings,
                                            SoundMatcher::ProgressCallback progress = {},
                                            SoundMatcher::CancelCallback cancel = {});
};
