#pragma once
#include <JuceHeader.h>

enum class AIProvider : int
{
    disabled = 0,
    openAI,
    gemini,
    openAICompatible,
    copilotBridge
};

struct AISettings
{
    bool enabled = false;
    AIProvider provider = AIProvider::disabled;
    juce::String model { "gpt-5.6-sol" };
    juce::String endpoint { "https://api.openai.com/v1/responses" };
    juce::String apiKeyEnvironment { "OPENAI_API_KEY" };
    juce::String sessionApiKey;
    bool featuresOnly = true;

    juce::String providerName() const;
    juce::String resolvedApiKey() const;
    bool hasUsableConfiguration() const;
    juce::String configurationHint() const;

    static AISettings load();
    void save() const;
    void applyProviderDefaults (AIProvider newProvider);
};
