#include "AISeedProvider.h"

namespace
{
constexpr int diagnosticPayloadLimit = 12000;

float numberProperty (const juce::DynamicObject& object, const juce::Identifier& name, float fallback)
{
    if (! object.hasProperty (name)) return fallback;
    const auto value = object.getProperty (name);
    if (value.isDouble() || value.isInt() || value.isInt64() || value.isBool()) return (float) value;
    return fallback;
}

int intProperty (const juce::DynamicObject& object, const juce::Identifier& name, int fallback)
{
    return (int) std::lround (numberProperty (object, name, (float) fallback));
}

juce::String stringProperty (const juce::DynamicObject& object, const juce::Identifier& name, const juce::String& fallback = {})
{
    if (! object.hasProperty (name)) return fallback;
    return object.getProperty (name).toString();
}

juce::String stripCodeFence (juce::String text)
{
    text = text.trim();
    if (! text.startsWith ("```")) return text;
    const int firstLine = text.indexOfChar ('\n');
    if (firstLine >= 0) text = text.substring (firstLine + 1);
    const int fence = text.lastIndexOf ("```");
    if (fence >= 0) text = text.substring (0, fence);
    return text.trim();
}

void appendDiagnosticPayload (juce::String& diagnostics, const juce::String& label, const juce::String& payload)
{
    diagnostics << "\n" << label << " (" << payload.length() << " chars";
    if (payload.length() > diagnosticPayloadLimit) diagnostics << ", first " << diagnosticPayloadLimit << " shown";
    diagnostics << "):\n" << payload.substring (0, diagnosticPayloadLimit) << "\n";
}

juce::String buildPrompt (const SoundFeatures& f, const VoiceParameters& base)
{
    juce::String prompt;
    prompt << "You are a synthesizer sound-design assistant. RetroMatch already analysed a reference audio file. "
           << "Propose exactly three deliberately different but plausible synthesizer parameter variants. "
           << "Do not describe DSP theory. Return ONLY JSON with one object containing a variants array. "
           << "Each variant must contain name, note, osc1Wave, osc2Wave, osc1Mix, osc2Mix, subMix, noiseMix, additiveMix, "
           << "wavetableMix, wavetablePosition, wavetableWarp, supersawMix, unisonDetune, unisonSpread, wavefold, "
           << "fmMix, fmFeedback, fmAlgorithm, harmonicTilt, oddEvenBalance, attack, decay, sustain, release, cutoff, "
           << "resonance, filterType, drive, chorusMix, delayMix, reverbMix and stereoWidth. "
           << "Ranges: wave 0..4; algorithm 0..5; filterType 0..2; mixes/amounts 0..1 except additiveMix<=0.85, "
           << "unisonDetune 0..70 cents, unisonSpread 0..1, harmonicTilt 0.45..3.5, attack/decay 0.001..5 s, "
           << "sustain 0..1, release 0.001..8 s, cutoff 20..20000 Hz, resonance 0.01..0.99, stereoWidth 0..2. "
           << "Make variant A natural/spectral, B FM/harmonic, C wavetable/unison/textural.\n\n"
           << "REFERENCE FEATURES:\n"
           << "duration=" << f.duration << ", rms=" << f.rms << ", peak=" << f.peak
           << ", f0=" << f.fundamentalHz << ", pitchConfidence=" << f.pitchConfidence
           << ", centroid=" << f.spectralCentroidHz << ", rolloff=" << f.spectralRolloffHz
           << ", bandwidth=" << f.spectralBandwidthHz << ", flatness=" << f.spectralFlatness
           << ", harmonicity=" << f.harmonicity << ", inharmonicity=" << f.inharmonicity
           << ", transient=" << f.transientScore << ", spectralMotion=" << f.spectralMotion
           << ", attack=" << f.attackSeconds << ", decay=" << f.decaySeconds
           << ", sustain=" << f.sustainLevel << ", release=" << f.releaseSeconds
           << ", stereoWidth=" << f.stereoWidth << ".\n"
           << "CURRENT SEED: osc1Wave=" << base.osc1Wave << ", osc2Wave=" << base.osc2Wave
           << ", osc1Mix=" << base.osc1Mix << ", osc2Mix=" << base.osc2Mix
           << ", wavetableMix=" << base.wavetableMix << ", fmMix=" << base.fmMix
           << ", cutoff=" << base.cutoff << ", attack=" << base.attack << ", release=" << base.release << ".";
    return prompt;
}

juce::String makeOpenAIRequest (const AISettings& settings, const juce::String& prompt)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("model", settings.model);
    object->setProperty ("input", prompt);
    // Do not send sampling controls blindly. Several GPT-5.x Responses models
    // reject temperature entirely; the model default is appropriate for seed generation.
    object->setProperty ("store", false);
    return juce::JSON::toString (juce::var (object), false);
}

juce::String makeGeminiRequest (const juce::String& prompt)
{
    auto* part = new juce::DynamicObject();
    part->setProperty ("text", prompt);
    juce::Array<juce::var> parts;
    parts.add (juce::var (part));

    auto* content = new juce::DynamicObject();
    content->setProperty ("parts", juce::var (parts));
    juce::Array<juce::var> contents;
    contents.add (juce::var (content));

    auto* generation = new juce::DynamicObject();
    generation->setProperty ("temperature", 0.45);
    generation->setProperty ("responseMimeType", "application/json");

    auto* root = new juce::DynamicObject();
    root->setProperty ("contents", juce::var (contents));
    root->setProperty ("generationConfig", juce::var (generation));
    return juce::JSON::toString (juce::var (root), false);
}

juce::String makeCompatibleRequest (const AISettings& settings, const juce::String& prompt)
{
    auto* message = new juce::DynamicObject();
    message->setProperty ("role", "user");
    message->setProperty ("content", prompt);
    juce::Array<juce::var> messages;
    messages.add (juce::var (message));

    auto* root = new juce::DynamicObject();
    root->setProperty ("model", settings.model);
    root->setProperty ("messages", juce::var (messages));
    root->setProperty ("temperature", 0.45);
    return juce::JSON::toString (juce::var (root), false);
}

juce::String extractProviderText (AIProvider provider, const juce::var& parsed)
{
    auto* root = parsed.getDynamicObject();
    if (root == nullptr) return {};

    if (provider == AIProvider::openAI)
    {
        auto direct = root->getProperty ("output_text").toString();
        if (direct.isNotEmpty()) return direct;

        if (auto* output = root->getProperty ("output").getArray())
            for (const auto& item : *output)
                if (auto* itemObject = item.getDynamicObject())
                    if (auto* content = itemObject->getProperty ("content").getArray())
                        for (const auto& entry : *content)
                            if (auto* entryObject = entry.getDynamicObject())
                                if (entryObject->getProperty ("type").toString() == "output_text")
                                    return entryObject->getProperty ("text").toString();
        return {};
    }

    if (provider == AIProvider::gemini)
    {
        if (auto* candidates = root->getProperty ("candidates").getArray())
            if (! candidates->isEmpty())
                if (auto* candidate = candidates->getReference (0).getDynamicObject())
                    if (auto* content = candidate->getProperty ("content").getDynamicObject())
                        if (auto* parts = content->getProperty ("parts").getArray())
                            if (! parts->isEmpty())
                                if (auto* part = parts->getReference (0).getDynamicObject())
                                    return part->getProperty ("text").toString();
        return {};
    }

    if (auto* choices = root->getProperty ("choices").getArray())
        if (! choices->isEmpty())
            if (auto* choice = choices->getReference (0).getDynamicObject())
                if (auto* message = choice->getProperty ("message").getDynamicObject())
                    return message->getProperty ("content").toString();
    return {};
}

bool postJson (const AISettings& settings,
               const juce::String& prompt,
               juce::String& responseText,
               juce::String& error,
               juce::String& diagnostics)
{
    auto endpoint = settings.endpoint;
    juce::String requestBody;
    juce::String headers = "Content-Type: application/json\r\n";

    if (settings.provider == AIProvider::openAI)
    {
        requestBody = makeOpenAIRequest (settings, prompt);
        headers << "Authorization: Bearer " << settings.resolvedApiKey() << "\r\n";
    }
    else if (settings.provider == AIProvider::gemini)
    {
        endpoint = endpoint.replace ("{model}", settings.model);
        requestBody = makeGeminiRequest (prompt);
        headers << "x-goog-api-key: " << settings.resolvedApiKey() << "\r\n";
    }
    else
    {
        requestBody = makeCompatibleRequest (settings, prompt);
        headers << "Authorization: Bearer " << settings.resolvedApiKey() << "\r\n";
    }

    diagnostics = "AI REQUEST\n";
    diagnostics << "Provider: " << settings.providerName() << "\n"
                << "Model: " << settings.model << "\n"
                << "Endpoint: " << endpoint << "\n"
                << "Audio upload: no (analysis features + current synth seed only)\n"
                << "API key: configured via "
                << (settings.sessionApiKey.isNotEmpty() ? juce::String ("session key")
                                                        : "environment variable " + settings.apiKeyEnvironment)
                << "\n";
    if (settings.provider == AIProvider::openAI)
        diagnostics << "Sampling controls: model default (temperature omitted for GPT-5.x compatibility)\n";

    int statusCode = 0;
    auto url = juce::URL (endpoint).withPOSTData (requestBody);
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withHttpRequestCmd ("POST")
                       .withExtraHeaders (headers)
                       .withConnectionTimeoutMs (30000)
                       .withNumRedirectsToFollow (2)
                       .withStatusCode (&statusCode);

    auto stream = url.createInputStream (options);
    if (stream == nullptr)
    {
        diagnostics << "HTTP status: unavailable\nResult: connection failed before a response stream was created.\n";
        error = "Could not connect to " + settings.providerName() + ". See AI LOG for details.";
        return false;
    }

    const auto raw = stream->readEntireStreamAsString();
    diagnostics << "HTTP status: " << statusCode << "\n"
                << "Raw response size: " << raw.length() << " chars\n";

    if (statusCode < 200 || statusCode >= 300)
    {
        appendDiagnosticPayload (diagnostics, "Provider response", raw);
        error = settings.providerName() + " returned HTTP " + juce::String (statusCode) + ". See AI LOG for the full provider response.";
        return false;
    }

    const auto parsed = juce::JSON::parse (raw);
    responseText = extractProviderText (settings.provider, parsed);
    if (responseText.isEmpty())
    {
        appendDiagnosticPayload (diagnostics, "Provider response", raw);
        error = "The provider returned no usable text response. See AI LOG for the raw response.";
        return false;
    }

    diagnostics << "Provider response decoded successfully.\n";
    return true;
}

void applySuggestion (VoiceParameters& p, const juce::DynamicObject& object)
{
    p.osc1Wave = juce::jlimit (0, 4, intProperty (object, "osc1Wave", p.osc1Wave));
    p.osc2Wave = juce::jlimit (0, 4, intProperty (object, "osc2Wave", p.osc2Wave));
    p.osc1Mix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "osc1Mix", p.osc1Mix));
    p.osc2Mix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "osc2Mix", p.osc2Mix));
    p.subMix = juce::jlimit (0.0f, 0.65f, numberProperty (object, "subMix", p.subMix));
    p.noiseMix = juce::jlimit (0.0f, 0.65f, numberProperty (object, "noiseMix", p.noiseMix));
    p.additiveMix = juce::jlimit (0.0f, 0.85f, numberProperty (object, "additiveMix", p.additiveMix));
    p.wavetableMix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "wavetableMix", p.wavetableMix));
    p.wavetablePosition = juce::jlimit (0.0f, 1.0f, numberProperty (object, "wavetablePosition", p.wavetablePosition));
    p.wavetableWarp = juce::jlimit (-1.0f, 1.0f, numberProperty (object, "wavetableWarp", p.wavetableWarp));
    p.supersawMix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "supersawMix", p.supersawMix));
    p.unisonDetune = juce::jlimit (0.0f, 70.0f, numberProperty (object, "unisonDetune", p.unisonDetune));
    p.unisonSpread = juce::jlimit (0.0f, 1.0f, numberProperty (object, "unisonSpread", p.unisonSpread));
    p.wavefold = juce::jlimit (0.0f, 1.0f, numberProperty (object, "wavefold", p.wavefold));
    p.fmMix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "fmMix", p.fmMix));
    p.fmFeedback = juce::jlimit (0.0f, 1.0f, numberProperty (object, "fmFeedback", p.fmFeedback));
    p.fmAlgorithm = juce::jlimit (0, 5, intProperty (object, "fmAlgorithm", p.fmAlgorithm));
    p.harmonicTilt = juce::jlimit (0.45f, 3.5f, numberProperty (object, "harmonicTilt", p.harmonicTilt));
    p.oddEvenBalance = juce::jlimit (0.0f, 1.0f, numberProperty (object, "oddEvenBalance", p.oddEvenBalance));
    p.attack = juce::jlimit (0.001f, 5.0f, numberProperty (object, "attack", p.attack));
    p.decay = juce::jlimit (0.001f, 5.0f, numberProperty (object, "decay", p.decay));
    p.sustain = juce::jlimit (0.0f, 1.0f, numberProperty (object, "sustain", p.sustain));
    p.release = juce::jlimit (0.001f, 8.0f, numberProperty (object, "release", p.release));
    p.cutoff = juce::jlimit (20.0f, 20000.0f, numberProperty (object, "cutoff", p.cutoff));
    p.resonance = juce::jlimit (0.01f, 0.99f, numberProperty (object, "resonance", p.resonance));
    p.filterType = juce::jlimit (0, 2, intProperty (object, "filterType", p.filterType));
    p.drive = juce::jlimit (0.0f, 1.0f, numberProperty (object, "drive", p.drive));
    p.chorusMix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "chorusMix", p.chorusMix));
    p.delayMix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "delayMix", p.delayMix));
    p.reverbMix = juce::jlimit (0.0f, 1.0f, numberProperty (object, "reverbMix", p.reverbMix));
    p.stereoWidth = juce::jlimit (0.0f, 2.0f, numberProperty (object, "stereoWidth", p.stereoWidth));
}
}

AIVariantBatch AISeedProvider::generateVariants (const SoundFeatures& reference,
                                                 const VoiceParameters& base,
                                                 const MatchSettings& matchSettings,
                                                 const AISettings& settings,
                                                 SoundMatcher::ProgressCallback progress,
                                                 SoundMatcher::CancelCallback cancel)
{
    AIVariantBatch batch;
    batch.providerSummary = settings.providerName() + " / " + settings.model;

    if (! settings.hasUsableConfiguration())
    {
        batch.error = settings.configurationHint();
        batch.diagnostics = "AI CONFIGURATION ERROR\n" + batch.error;
        return batch;
    }
    if (cancel && cancel()) { batch.error = "AI matching cancelled."; batch.diagnostics = batch.error; return batch; }
    if (progress) progress (0.05f);

    juce::String providerText;
    if (! postJson (settings, buildPrompt (reference, base), providerText, batch.error, batch.diagnostics)) return batch;
    if (cancel && cancel()) { batch.error = "AI matching cancelled."; batch.diagnostics << "\n" << batch.error; return batch; }
    if (progress) progress (0.45f);

    auto suggestionJson = juce::JSON::parse (stripCodeFence (providerText));
    auto* root = suggestionJson.getDynamicObject();
    if (root == nullptr)
    {
        batch.error = "AI response was not a JSON object. See AI LOG for the decoded provider text.";
        appendDiagnosticPayload (batch.diagnostics, "Decoded provider text", providerText);
        return batch;
    }

    auto* variants = root->getProperty ("variants").getArray();
    if (variants == nullptr || variants->size() < 3)
    {
        batch.error = "AI response did not contain three variants. See AI LOG for the decoded provider text.";
        appendDiagnosticPayload (batch.diagnostics, "Decoded provider text", providerText);
        return batch;
    }

    for (int i = 0; i < 3; ++i)
    {
        if (cancel && cancel()) { batch.error = "AI matching cancelled."; batch.diagnostics << "\n" << batch.error; return batch; }
        auto* suggestion = variants->getReference (i).getDynamicObject();
        if (suggestion == nullptr)
        {
            batch.error = "AI variant " + juce::String (i + 1) + " was invalid. See AI LOG for the decoded provider text.";
            appendDiagnosticPayload (batch.diagnostics, "Decoded provider text", providerText);
            return batch;
        }

        auto params = base;
        applySuggestion (params, *suggestion);
        params.referenceWavetable = base.referenceWavetable;
        auto evaluated = SoundMatcher::evaluateFit (reference, params, matchSettings);
        evaluated.explanation = "AI " + stringProperty (*suggestion, "name", juce::String::charToString ((juce_wchar) ('A' + i)))
                              + ": " + stringProperty (*suggestion, "note", "Provider-generated seed, locally rendered and scored.");
        batch.candidates[(size_t) i] = std::move (evaluated);
        if (progress) progress (0.45f + 0.55f * (float) (i + 1) / 3.0f);
    }

    batch.diagnostics << "Result: three AI variants parsed, rendered and scored locally.\n";
    return batch;
}
