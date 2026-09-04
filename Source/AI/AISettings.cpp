#include "AISettings.h"

namespace
{
juce::PropertiesFile::Options makeOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "RetroMatchSynth";
    options.filenameSuffix = "settings";
    options.folderName = "AlAi Audio/RetroMatch Synth";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}
}

juce::String AISettings::providerName() const
{
    switch (provider)
    {
        case AIProvider::openAI: return "OpenAI";
        case AIProvider::gemini: return "Google Gemini";
        case AIProvider::openAICompatible: return "OpenAI-compatible / Azure";
        case AIProvider::copilotBridge: return "GitHub Copilot bridge";
        default: return "Disabled";
    }
}

juce::String AISettings::resolvedApiKey() const
{
    if (sessionApiKey.isNotEmpty()) return sessionApiKey;
    if (apiKeyEnvironment.isEmpty()) return {};
    return juce::SystemStats::getEnvironmentVariable (apiKeyEnvironment, {});
}

bool AISettings::hasUsableConfiguration() const
{
    if (! enabled || provider == AIProvider::disabled) return false;
    if (provider == AIProvider::copilotBridge && endpoint.isEmpty()) return false;
    return model.isNotEmpty() && endpoint.isNotEmpty() && resolvedApiKey().isNotEmpty();
}

juce::String AISettings::configurationHint() const
{
    if (! enabled || provider == AIProvider::disabled)
        return "AI matching is disabled. Local DSP matching remains fully available.";
    if (provider == AIProvider::copilotBridge && endpoint.isEmpty())
        return "GitHub Copilot has no generic plug-in inference endpoint. Configure an external OpenAI-compatible bridge URL.";
    if (model.isEmpty()) return "Choose a model.";
    if (endpoint.isEmpty()) return "Configure the provider endpoint.";
    if (resolvedApiKey().isEmpty())
        return "No API key found. Enter a session key or define environment variable " + apiKeyEnvironment + ".";
    return "Ready. RetroMatch sends extracted analysis features only; audio stays local.";
}

AISettings AISettings::load()
{
    juce::PropertiesFile props (makeOptions());
    AISettings settings;
    settings.enabled = props.getBoolValue ("ai.enabled", false);
    settings.provider = static_cast<AIProvider> (juce::jlimit (0, 4, props.getIntValue ("ai.provider", 0)));
    settings.model = props.getValue ("ai.model", settings.model);
    settings.endpoint = props.getValue ("ai.endpoint", settings.endpoint);
    settings.apiKeyEnvironment = props.getValue ("ai.keyEnvironment", settings.apiKeyEnvironment);
    settings.featuresOnly = props.getBoolValue ("ai.featuresOnly", true);
    return settings;
}

void AISettings::save() const
{
    juce::PropertiesFile props (makeOptions());
    props.setValue ("ai.enabled", enabled);
    props.setValue ("ai.provider", static_cast<int> (provider));
    props.setValue ("ai.model", model);
    props.setValue ("ai.endpoint", endpoint);
    props.setValue ("ai.keyEnvironment", apiKeyEnvironment);
    props.setValue ("ai.featuresOnly", featuresOnly);
    props.saveIfNeeded();
}

void AISettings::applyProviderDefaults (AIProvider newProvider)
{
    provider = newProvider;
    switch (provider)
    {
        case AIProvider::openAI:
            model = "gpt-5.6-sol";
            endpoint = "https://api.openai.com/v1/responses";
            apiKeyEnvironment = "OPENAI_API_KEY";
            break;
        case AIProvider::gemini:
            model = "gemini-3.7-flash";
            endpoint = "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent";
            apiKeyEnvironment = "GEMINI_API_KEY";
            break;
        case AIProvider::openAICompatible:
            model = "";
            endpoint = "";
            apiKeyEnvironment = "RETROMATCH_AI_API_KEY";
            break;
        case AIProvider::copilotBridge:
            model = "auto";
            endpoint = "";
            apiKeyEnvironment = "RETROMATCH_COPILOT_BRIDGE_KEY";
            break;
        default:
            model = "";
            endpoint = "";
            apiKeyEnvironment = "";
            break;
    }
}
