from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one marker, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path, pattern, replacement):
    text = read(path)
    new, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{path}: regex marker not found: {pattern[:120]!r}")
    write(path, new)


# Match metadata: Gold candidates must remember the method/depth and whether the
# reported similarity is for the completed multi-instance rack rather than one voice.
replace_once(
    "Source/Matching/SoundMatcher.h",
    "    float effectProbeSimilarity = -1.0f;\n    juce::String explanation;",
    "    float effectProbeSimilarity = -1.0f;\n"
    "    int algorithm = -1;      // selected resynthesis strategy when the result owns one\n"
    "    int complexity = -1;     // selected 1-3 / 4 / 6 / 8 rack depth when the result owns one\n"
    "    bool fullRackScore = false; // similarity was measured after all embedded layers rendered\n"
    "    juce::String explanation;"
)

# Public processor entry point.
replace_once(
    "Source/PluginProcessor.h",
    "    std::array<MatchResult, 3> buildCandidateBank();\n",
    "    std::array<MatchResult, 3> buildCandidateBank();\n"
    "    std::array<MatchResult, 3> buildGoldCandidateBank (SoundMatcher::ProgressCallback progress = {},\n"
    "                                                       SoundMatcher::CancelCallback cancel = {});\n"
)

# Processor helpers / advisor access.
replace_once(
    "Source/PluginProcessor.cpp",
    '#include "UI/PresetsPage.h"\n#include <algorithm>\n#include <cmath>\n',
    '#include "UI/PresetsPage.h"\n#include "Matching/ResynthesisAdvisor.h"\n#include <algorithm>\n#include <cmath>\n#include <vector>\n'
)

# Make the table pointer type match VoiceParameters, so deep candidates can carry
# their exact extracted/chopped table into the embedded rack.
replace_once(
    "Source/PluginProcessor.cpp",
    "VoiceParameters makeResynthCompanion (const VoiceParameters& source, int role, int strategy,\n                                      const std::shared_ptr<ReferenceWavetableData>& table)",
    "VoiceParameters makeResynthCompanion (const VoiceParameters& source, int role, int strategy,\n                                      const std::shared_ptr<const ReferenceWavetableData>& table)"
)

rack_helper = r'''

VoiceParameters makeEmbeddedResynthRack (const MatchResult& mainResult, int complexity, int strategy,
                                         const std::shared_ptr<const ReferenceWavetableData>& table)
{
    auto rack = mainResult.params;
    rack.layers.fill (nullptr);
    complexity = juce::jlimit (0, 3, complexity);
    strategy = juce::jlimit (0, 6, strategy);

    // Gold evaluates the actual completed instrument. Classic intentionally uses
    // a strong three-instance rack (the upper end of the legacy 1-3 range), while
    // the other choices map directly to 4 / 6 / 8 total instances.
    const int totalInstances = complexity == 0 ? 3 : (complexity == 1 ? 4 : (complexity == 2 ? 6 : 8));
    rack.mainLayerGain = totalInstances >= 8 ? 0.64f : (totalInstances >= 6 ? 0.70f : 0.78f);

    static const float roleGain[] { 0.30f, 0.18f, 0.24f, 0.20f, 0.17f, 0.16f, 0.13f };
    static const float rolePan[]  { -0.10f, 0.34f, 0.0f, -0.30f, 0.18f, 0.42f, -0.42f };
    static const float roleTune[] { 0.0f, 12.0f, -12.0f, 0.0f, 7.0f, 0.0f, 12.0f };

    const int wantedLayers = juce::jmin (VoiceParameters::extraLayerCount, totalInstances - 1);
    for (int layer = 0; layer < wantedLayers; ++layer)
    {
        auto companion = makeResynthCompanion (mainResult.params, layer, strategy, table);
        companion.layers.fill (nullptr);
        companion.mainLayerGain = 1.0f;
        rack.layers[(size_t) layer] = std::make_shared<VoiceParameters> (std::move (companion));
        rack.layerGain[(size_t) layer] = roleGain[layer];
        rack.layerPan[(size_t) layer] = rolePan[layer];
        rack.layerTune[(size_t) layer] = roleTune[layer];
        rack.layerOperation[(size_t) layer] = 0;
        rack.layerAmount[(size_t) layer] = 0.78f;
    }
    return rack;
}
'''
replace_once(
    "Source/PluginProcessor.cpp",
    "    return p;\n}\n\nclass OversamplingQualityEditor",
    "    return p;\n}" + rack_helper + "\nclass OversamplingQualityEditor"
)

# Apply an embedded Gold rack exactly as it was measured. Ordinary Quick/Refine
# results retain the existing generated-rack behaviour.
new_apply = r'''void RetroMatchSynthAudioProcessor::applyGeneratedRack (const MatchResult& mainResult, int selectedBankIndex)
{
    selectEditingLayer (-1);
    allEditorNotesOff();
    for (int i = 0; i < VoiceParameters::extraLayerCount; ++i) clearLayer (i);

    const int strategy = mainResult.algorithm >= 0
                       ? juce::jlimit (0, 6, mainResult.algorithm)
                       : juce::jlimit (0, 6, (int) apvts.getRawParameterValue ("resynthStrategy")->load());
    const int currentComplexity = juce::jlimit (0, 3, (int) apvts.getRawParameterValue ("resynthComplexity")->load());
    const int complexity = mainResult.complexity >= 0 ? juce::jlimit (0, 3, mainResult.complexity) : currentComplexity;

    bool hasEmbeddedRack = false;
    for (const auto& layer : mainResult.params.layers) hasEmbeddedRack |= layer != nullptr;
    if (hasEmbeddedRack)
    {
        if (auto* mainGain = apvts.getParameter ("mainLayerGain"))
            mainGain->setValueNotifyingHost (mainGain->convertTo0to1 (mainResult.params.mainLayerGain));

        MatchResult mainOnly = mainResult;
        mainOnly.params.layers.fill (nullptr);
        applyMatchResult (mainOnly);

        static const char* roleNames[] { "BODY", "AIR", "FOUNDATION", "MOTION", "HARMONIC", "WIDTH", "TEXTURE" };
        for (int layer = 0; layer < VoiceParameters::extraLayerCount; ++layer)
        {
            const auto& storedLayer = mainResult.params.layers[(size_t) layer];
            if (! storedLayer) continue;
            MatchResult layerResult; layerResult.params = *storedLayer;
            applyMatchResult (layerResult);
            captureLayer (layer);

            auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
            auto stored = bank.getChildWithProperty ("index", layer);
            if (stored.isValid()) stored.setProperty ("name", "GOLD / " + juce::String (roleNames[layer]), nullptr);

            const auto prefix = "layer" + juce::String (layer + 1);
            auto setLayer = [this, &prefix] (const char* suffix, float value)
            {
                if (auto* parameter = apvts.getParameter (prefix + suffix))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            setLayer ("Gain", mainResult.params.layerGain[(size_t) layer]);
            setLayer ("Pan", mainResult.params.layerPan[(size_t) layer]);
            setLayer ("Tune", mainResult.params.layerTune[(size_t) layer]);
            setLayer ("Operation", (float) mainResult.params.layerOperation[(size_t) layer]);
            setLayer ("Amount", mainResult.params.layerAmount[(size_t) layer]);
        }

        // Restore the exact measured main voice and its full-rack similarity metadata.
        applyMatchResult (mainResult);
        return;
    }

    if (auto* mainGain = apvts.getParameter ("mainLayerGain"))
        mainGain->setValueNotifyingHost (mainGain->convertTo0to1 (1.0f));
    applyMatchResult (mainResult);
    const int legacyInstances = juce::jlimit (1, 3, 1 + (int) apvts.getRawParameterValue ("resynthInstances")->load());
    const int totalInstances = complexity == 0 ? legacyInstances : (complexity == 1 ? 4 : complexity == 2 ? 6 : 8);

    std::array<int, 2> complement {{ -1, -1 }};
    int complementCount = 0;
    for (int candidate = 0; candidate < 3 && complementCount < 2; ++candidate)
        if (candidate != selectedBankIndex && candidateBank[(size_t) candidate].confidence > 0.0f)
            complement[(size_t) complementCount++] = candidate;
    if (complementCount == 2 && candidateBank[(size_t) complement[1]].similarity.total > candidateBank[(size_t) complement[0]].similarity.total)
        std::swap (complement[0], complement[1]);

    static const char* roleNames[] { "BODY", "AIR", "FOUNDATION", "MOTION", "HARMONIC", "WIDTH", "TEXTURE" };
    static const float roleGain[] { 0.30f, 0.18f, 0.24f, 0.20f, 0.17f, 0.16f, 0.13f };
    static const float rolePan[]  { -0.10f, 0.34f, 0.0f, -0.30f, 0.18f, 0.42f, -0.42f };
    static const float roleTune[] { 0.0f, 12.0f, -12.0f, 0.0f, 7.0f, 0.0f, 12.0f };

    const int wantedLayers = juce::jmin (VoiceParameters::extraLayerCount, totalInstances - 1);
    for (int layer = 0; layer < wantedLayers; ++layer)
    {
        VoiceParameters source = mainResult.params;
        if (layer < complementCount) source = candidateBank[(size_t) complement[(size_t) layer]].params;
        source.layers.fill (nullptr);
        auto companion = makeResynthCompanion (source, layer, strategy, referenceWavetable);
        MatchResult layerResult; layerResult.params = companion;
        applyMatchResult (layerResult);
        captureLayer (layer);

        auto bank = apvts.state.getChildWithName ("SYNTH_LAYERS");
        auto stored = bank.getChildWithProperty ("index", layer);
        if (stored.isValid()) stored.setProperty ("name", "RESYNTH / " + juce::String (roleNames[layer]), nullptr);

        const auto prefix = "layer" + juce::String (layer + 1);
        auto setLayer = [this, &prefix] (const char* suffix, float value)
        {
            if (auto* parameter = apvts.getParameter (prefix + suffix))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };
        setLayer ("Gain", roleGain[layer]);
        setLayer ("Pan", rolePan[layer]);
        setLayer ("Tune", roleTune[layer]);
        setLayer ("Operation", 0.0f);
        setLayer ("Amount", 0.78f);
    }

    applyMatchResult (mainResult);
}

void RetroMatchSynthAudioProcessor::updateCandidatePreview'''
regex_once(
    "Source/PluginProcessor.cpp",
    r"void RetroMatchSynthAudioProcessor::applyGeneratedRack \(const MatchResult& mainResult, int selectedBankIndex\)\n\{.*?\n\}\n\nvoid RetroMatchSynthAudioProcessor::updateCandidatePreview",
    new_apply
)

gold_function = r'''

std::array<MatchResult, 3> RetroMatchSynthAudioProcessor::buildGoldCandidateBank (SoundMatcher::ProgressCallback progress,
                                                                                 SoundMatcher::CancelCallback cancel)
{
    std::array<MatchResult, 3> result {};
    if (! currentFeatures) return result;

    const auto reference = *currentFeatures;
    const auto advice = ResynthesisAdvisor::advise (reference);
    const auto authored = getMainVoiceParameters();

    std::vector<int> methods;
    methods.reserve (7);
    methods.push_back (juce::jlimit (0, 6, advice.method));
    for (int method = 0; method <= 6; ++method)
        if (std::find (methods.begin(), methods.end(), method) == methods.end()) methods.push_back (method);

    std::vector<MatchResult> coarse;
    coarse.reserve (methods.size());
    for (size_t index = 0; index < methods.size(); ++index)
    {
        if (cancel && cancel()) return {};
        const int method = methods[index];
        auto seed = SoundMatcher::initialFit (reference).params;

        std::shared_ptr<const ReferenceWavetableData> table = referenceWavetable;
        if (method == 5 && loadedReferenceFile.existsAsFile())
            if (auto chopped = ReferenceWavetableExtractor::chop (loadedReferenceFile, analysisStartSeconds.load(), analysisEndSeconds.load()))
                table = std::move (chopped);

        seed.referenceWavetable = table;
        seed.referenceWavetableMix = table ? referenceTableWeight (reference, method) : 0.0f;
        seed.userWavetable = userWavetable;
        seed.userWavetableMix = authored.userWavetableMix;
        seed.distortionMode = authored.distortionMode;
        seed.distortionMix = authored.distortionMix;
        seed.layers.fill (nullptr);
        seed.mainLayerGain = 1.0f;

        auto settings = matchSettings;
        settings.algorithm = method;
        settings.iterations = juce::jlimit (18, 42, juce::jmax (18, matchSettings.iterations / 4));
        settings.topologyTrials = juce::jlimit (6, 12, juce::jmax (6, matchSettings.topologyTrials / 2));
        settings.populationSize = juce::jlimit (4, 6, matchSettings.populationSize);
        auto candidate = SoundMatcher::refineFit (reference, seed, settings, {}, cancel);
        candidate.algorithm = method;
        candidate.complexity = advice.complexity;
        candidate.explanation = "GOLD coarse method sweep / " + ResynthesisAdvice { method, advice.complexity, 0.0f, {}, {} }.methodName()
                              + ". " + candidate.explanation;
        coarse.push_back (std::move (candidate));
        if (progress) progress (0.25f * (float) (index + 1) / (float) methods.size());
    }

    std::sort (coarse.begin(), coarse.end(), [] (const MatchResult& a, const MatchResult& b)
    {
        return a.similarity.total > b.similarity.total;
    });
    if (coarse.size() > 3) coarse.resize (3);

    std::vector<MatchResult> finals;
    finals.reserve (coarse.size());
    for (size_t index = 0; index < coarse.size(); ++index)
    {
        if (cancel && cancel()) return {};
        const int method = coarse[index].algorithm;
        auto deepSettings = matchSettings;
        deepSettings.algorithm = method;
        deepSettings.iterations = juce::jmax (84, matchSettings.iterations);
        deepSettings.topologyTrials = juce::jmax (16, matchSettings.topologyTrials);
        deepSettings.populationSize = juce::jmax (6, matchSettings.populationSize);
        const float base = 0.25f + (float) index * (0.50f / 3.0f);
        const float span = 0.50f / 3.0f;
        auto deep = SoundMatcher::refineFit (reference, coarse[index].params, deepSettings,
            [progress, base, span] (float p) { if (progress) progress (base + span * p); }, cancel);
        deep.algorithm = method;

        MatchResult bestFull;
        bestFull.similarity.total = -1.0f;
        for (int complexity = 0; complexity < 4; ++complexity)
        {
            if (cancel && cancel()) return {};
            auto rack = makeEmbeddedResynthRack (deep, complexity, method, deep.params.referenceWavetable);
            auto scoreSettings = matchSettings;
            scoreSettings.algorithm = method;
            auto full = SoundMatcher::evaluateFit (reference, rack, scoreSettings);
            full.algorithm = method;
            full.complexity = complexity;
            full.fullRackScore = true;
            full.evaluatedCandidates += deep.evaluatedCandidates;
            full.explanation = "GOLD full-rack verification: the completed "
                             + juce::String (complexity == 0 ? 3 : (complexity == 1 ? 4 : complexity == 2 ? 6 : 8))
                             + "-instance instrument was rendered and scored after layering. " + deep.explanation;
            if (full.similarity.total > bestFull.similarity.total) bestFull = std::move (full);

            const float rackProgress = ((float) index * 4.0f + (float) complexity + 1.0f) / 12.0f;
            if (progress) progress (0.75f + rackProgress * 0.25f);
        }
        finals.push_back (std::move (bestFull));
    }

    std::sort (finals.begin(), finals.end(), [] (const MatchResult& a, const MatchResult& b)
    {
        return a.similarity.total > b.similarity.total;
    });
    for (size_t i = 0; i < result.size() && i < finals.size(); ++i) result[i] = std::move (finals[i]);
    if (progress) progress (1.0f);
    return result;
}
'''
replace_once(
    "Source/PluginProcessor.cpp",
    "    selectedCandidate = 0; applyGeneratedRack (candidateBank[0], 0);\n    return candidateBank;\n}\n\nbool RetroMatchSynthAudioProcessor::selectCandidate (int index)",
    "    selectedCandidate = 0; applyGeneratedRack (candidateBank[0], 0);\n    return candidateBank;\n}" + gold_function + "\nbool RetroMatchSynthAudioProcessor::selectCandidate (int index)"
)

# Candidate selection must restore the method/depth that was actually measured.
regex_once(
    "Source/PluginProcessor.cpp",
    r"bool RetroMatchSynthAudioProcessor::selectCandidate \(int index\)\n\{.*?\n\}\n\nvoid RetroMatchSynthAudioProcessor::morphCandidates",
    r'''bool RetroMatchSynthAudioProcessor::selectCandidate (int index)
{
    if (! juce::isPositiveAndBelow (index, 3) || candidateBank[(size_t) index].confidence <= 0.0f) return false;
    selectedCandidate = index;
    const auto& selected = candidateBank[(size_t) index];
    auto setChoice = [this] (const char* id, int value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
    };
    if (selected.algorithm >= 0) setChoice ("resynthStrategy", juce::jlimit (0, 6, selected.algorithm));
    if (selected.complexity >= 0) setChoice ("resynthComplexity", juce::jlimit (0, 3, selected.complexity));
    applyGeneratedRack (selected, index);
    return true;
}

void RetroMatchSynthAudioProcessor::morphCandidates'''
)

# UI: Gold is a dedicated slow/full-rack mode, not a cosmetic alias of Refine.
replace_once("Source/UI/RetroMatchEditorV3.h", "    enum class WorkMode { quick, refine, ai };", "    enum class WorkMode { quick, refine, gold, ai };")
replace_once(
    "Source/UI/RetroMatchEditorV3.h",
    '    juce::TextButton load { "LOAD REFERENCE" }, quick { "QUICK x3" }, refine { "REFINE x3" }, aiVariants { "AI x3" }, compareMatch { "COMPARE" };',
    '    juce::TextButton load { "LOAD REFERENCE" }, quick { "QUICK x3" }, refine { "REFINE x3" }, goldMatch { "GOLD" }, aiVariants { "AI x3" }, compareMatch { "COMPARE" };'
)

# Candidate cards identify heterogeneous Gold method/depth results.
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    g.setColour (juce::Colour (0xffb9c6c1));\n    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));\n    g.drawText (family, 56, 6, getWidth() - 135, 18, juce::Justification::centredLeft, true);",
    "    g.setColour (juce::Colour (0xffb9c6c1));\n"
    "    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));\n"
    "    juce::String familyText = family;\n"
    "    if (hasResult && result.algorithm >= 0)\n"
    "    {\n"
    "        const juce::StringArray methods { \"Balanced Hybrid\", \"Reference Wavetable\", \"Spectral Subtractive\",\n"
    "                                           \"FM / Harmonic\", \"Layered Studio\", \"Texture / Chop\", \"FX / Guitar Chain\" };\n"
    "        const juce::StringArray depths { \"Classic / 1-3\", \"Studio / 4\", \"Deep / 6\", \"Maximum / 8\" };\n"
    "        familyText = methods[juce::jlimit (0, methods.size() - 1, result.algorithm)];\n"
    "        if (result.fullRackScore && result.complexity >= 0) familyText << \" / \" << depths[juce::jlimit (0, 3, result.complexity)] << \" / FULL RACK\";\n"
    "    }\n"
    "    g.drawText (familyText, 56, 6, getWidth() - 135, 18, juce::Justification::centredLeft, true);"
)
replace_once("Source/UI/RetroMatchEditorV3.cpp", '        g.drawText ("Run Quick, Refine or AI", 28, 29, getWidth() - 40, getHeight() - 34, juce::Justification::centredLeft);', '        g.drawText ("Run Quick, Refine, Gold or AI", 28, 29, getWidth() - 40, getHeight() - 34, juce::Justification::centredLeft);')

# Thread name, callbacks and button visibility.
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    '    : juce::Thread (modeIn == WorkMode::ai ? "RetroMatch AI variants" : "RetroMatch local variants"), owner (ownerIn), mode (modeIn)',
    '    : juce::Thread (modeIn == WorkMode::ai ? "RetroMatch AI variants" : (modeIn == WorkMode::gold ? "RetroMatch Gold full-rack match" : "RetroMatch local variants")), owner (ownerIn), mode (modeIn)'
)
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    quick.onClick = [this] { startVariantSearch (WorkMode::quick); };\n    refine.onClick = [this] { startVariantSearch (WorkMode::refine); };\n    aiVariants.onClick = [this] { startVariantSearch (WorkMode::ai); };\n    for (auto* button : { &load, &quick, &refine, &aiVariants }) addAndMakeVisible (*button);",
    "    quick.onClick = [this] { startVariantSearch (WorkMode::quick); };\n"
    "    refine.onClick = [this] { startVariantSearch (WorkMode::refine); };\n"
    "    goldMatch.onClick = [this] { startVariantSearch (WorkMode::gold); };\n"
    "    goldMatch.setTooltip (\"Exhaustive heterogeneous search. Tries every resynthesis method, deeply optimizes the strongest three, tests all rack depths, then scores the completed multi-instance instrument.\");\n"
    "    aiVariants.onClick = [this] { startVariantSearch (WorkMode::ai); };\n"
    "    for (auto* button : { &load, &quick, &refine, &goldMatch, &aiVariants }) addAndMakeVisible (*button);"
)

# Every enable/disable set includes Gold.
text = read("Source/UI/RetroMatchEditorV3.cpp")
text = text.replace("{ &load, &quick, &refine, &aiVariants }", "{ &load, &quick, &refine, &goldMatch, &aiVariants }")
write("Source/UI/RetroMatchEditorV3.cpp", text)

# Five workflow actions in the same hardware row: QUICK / REFINE / GOLD / AI / COMPARE.
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    auto actionRow = w.removeFromTop (32);\n    const int buttonW = actionRow.getWidth() / 4;\n    quick.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    refine.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    aiVariants.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n    compareMatch.setBounds (actionRow.reduced (2, 0));",
    "    auto actionRow = w.removeFromTop (32);\n"
    "    const int buttonW = actionRow.getWidth() / 5;\n"
    "    quick.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n"
    "    refine.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n"
    "    goldMatch.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n"
    "    aiVariants.setBounds (actionRow.removeFromLeft (buttonW).reduced (2, 0));\n"
    "    compareMatch.setBounds (actionRow.reduced (2, 0));"
)

# Start status explains the expensive mode.
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    status.setText (mode == WorkMode::quick ? \"Quick matching: rendering three distinct local variants...\"\n                   : mode == WorkMode::refine ? \"Refining three variant families with the closed-loop optimizer...\"\n                                              : \"AI is proposing three seeds; RetroMatch will render and score them locally...\",\n                    juce::dontSendNotification);",
    "    status.setText (mode == WorkMode::quick ? \"Quick matching: rendering three distinct local variants...\"\n"
    "                   : mode == WorkMode::refine ? \"Refining three variant families with the closed-loop optimizer...\"\n"
    "                   : mode == WorkMode::gold ? \"GOLD: sweeping all methods, deep-refining the strongest three and scoring every completed rack depth...\"\n"
    "                                            : \"AI is proposing three seeds; RetroMatch will render and score them locally...\",\n"
    "                    juce::dontSendNotification);"
)

# Gold branch runs the processor-level full-rack search and returns through the same
# candidate/result pipeline as Quick/Refine/AI.
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "void RetroMatchSynthAudioProcessorEditor::runVariantSearch (WorkMode mode, VariantThread& thread)\n{\n    if (mode == WorkMode::ai)",
    "void RetroMatchSynthAudioProcessorEditor::runVariantSearch (WorkMode mode, VariantThread& thread)\n{\n"
    "    if (mode == WorkMode::gold)\n"
    "    {\n"
    "        auto results = proc.buildGoldCandidateBank (\n"
    "            [this] (float p) { matchProgress.store (juce::jlimit (0.0f, 1.0f, p)); },\n"
    "            [&thread] { return thread.threadShouldExit(); });\n"
    "        if (thread.threadShouldExit()) return;\n"
    "        juce::Component::SafePointer<RetroMatchSynthAudioProcessorEditor> safe (this);\n"
    "        juce::MessageManager::callAsync ([safe, results = std::move (results)] () mutable\n"
    "        {\n"
    "            if (safe != nullptr) safe->finishVariantSearch (std::move (results), \"GOLD / FULL RACK\");\n"
    "        });\n"
    "        return;\n"
    "    }\n\n"
    "    if (mode == WorkMode::ai)"
)

# Compare explicitly labels when the number is a complete rack measurement.
replace_once(
    "Source/UI/MatchCompareDialog.h",
    '        g.drawText ("METHOD  " + methods[methodIndex] + "    /    DEPTH  " + depths[depthIndex],\n                    24, 38, getWidth() - 360, 16, juce::Justification::centredLeft, true);',
    '        const juce::String rackTag = proc.lastMatch.fullRackScore ? "    /    GOLD FULL-RACK SCORE" : juce::String {};\n'
    '        g.drawText ("METHOD  " + methods[methodIndex] + "    /    DEPTH  " + depths[depthIndex] + rackTag,\n'
    '                    24, 38, getWidth() - 360, 16, juce::Justification::centredLeft, true);'
)

# Static source integrity should enforce the new architecture in future changes.
replace_once(
    "scripts/static-check.py",
    "    'v1 candidate bank': ['buildCandidateBank', 'morphCandidates', 'selectCandidate'],",
    "    'v1 candidate bank': ['buildCandidateBank', 'morphCandidates', 'selectCandidate'],\n"
    "    'gold full rack search': ['buildGoldCandidateBank', 'makeEmbeddedResynthRack', 'fullRackScore', 'GOLD / FULL RACK'],"
)
replace_once(
    "scripts/static-check.py",
    "    'v1 candidate bank': processor + editor_all,",
    "    'v1 candidate bank': processor + editor_all,\n"
    "    'gold full rack search': processor + processor_h + editor_all,"
)

# Keep the implementation plan aligned with the actual search architecture.
plan = read("plan.md")
if "Gold Match full-rack" not in plan:
    plan += """

## Gold Match full-rack resynthesis

- Add a dedicated GOLD search mode after the fast Quick/Refine paths.
- Sweep all supported synthesis explanations, with the Resynthesis Advisor recommendation evaluated first.
- Deep-refine the strongest heterogeneous methods rather than producing three variants of one topology.
- For each finalist, build and render the complete 3 / 4 / 6 / 8-instance rack and choose depth by measured reference similarity.
- Store the exact embedded rack in the winning candidate so selecting A/B/C reproduces what was measured.
- Keep FX white-noise/impulse probing secondary to the musical reference score; never inflate the displayed similarity to meet a target.
- Show method, depth and FULL RACK status in candidate cards and Compare.
"""
    write("plan.md", plan)

print("Gold Match migration applied")
