#include <JuceHeader.h>
#include "../Source/Analysis/SampleAnalyzer.h"
#include "../Source/Engine/MSEG.h"
#include "../Source/Engine/PatchGraph.h"
#include "../Source/Engine/DspRoutingPlan.h"
#include "../Source/Engine/ReferenceWavetable.h"
#include "../Source/Engine/PresetLibrary.h"
#include "../Source/Matching/OfflineRenderer.h"
#include "../Source/Matching/SoundMatcher.h"
#include "../Source/Matching/EffectChainProbe.h"
#include "../Source/Matching/ResynthesisAdvisor.h"
#include <cmath>
#include <iostream>

namespace
{
bool finiteFeatures (const SoundFeatures& f)
{
    if (! std::isfinite (f.rms) || ! std::isfinite (f.spectralCentroidHz) || ! std::isfinite (f.spectralMotion) || ! std::isfinite (f.inharmonicity)) return false;
    for (const auto value : f.spectralBands) if (! std::isfinite (value)) return false;
    for (const auto& frame : f.temporalSpectralBands)
        for (const auto value : frame) if (! std::isfinite (value)) return false;
    for (const auto value : f.timbreCepstrum) if (! std::isfinite (value)) return false;
    return true;
}

bool finiteAudio (const juce::AudioBuffer<float>& audio)
{
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (! std::isfinite (audio.getSample (ch, i))) return false;
    return true;
}

float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return std::numeric_limits<float>::infinity();

    float difference = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            difference = std::max (difference, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return difference;
}

int fail (const char* message)
{
    std::cerr << "FAILED: " << message << '\n';
    return 1;
}
}

bool runMelodyTests();
int main (int argc, char** argv)
{
    if (! runMelodyTests()) return 1;
    {
        PatchGraph::Document graph;
        auto addAudioNode = [&] (juce::String id, PatchGraph::NodeType type, bool input, bool output, bool multipleInput = false)
        {
            PatchGraph::Node node; node.id = std::move (id); node.type = type;
            if (input) node.ports.push_back (PatchGraph::audioInput (multipleInput));
            if (output) node.ports.push_back (PatchGraph::audioOutput());
            if (! graph.addNode (std::move (node))) return false;
            return true;
        };
        PatchGraph::Node mainFx; mainFx.id = "L-1:S4"; mainFx.type = PatchGraph::NodeType::processor; mainFx.routingMode = 1;
        if (! graph.addNode (mainFx)
            || ! addAudioNode ("L0:S5", PatchGraph::NodeType::mixer, false, true)
            || ! addAudioNode ("L1:S5", PatchGraph::NodeType::mixer, false, true)
            || ! addAudioNode ("GLOBALBUS", PatchGraph::NodeType::processor, true, true, true)
            || ! addAudioNode ("MASTER", PatchGraph::NodeType::master, true, false))
            return fail ("routing compiler graph fixture could not be built");

        PatchGraph::Edge layer0 { "combine:0", "L0:S5", "audio.out", "GLOBALBUS", "audio.in", PatchGraph::PortType::audio, true };
        PatchGraph::Edge layer1 { "combine:1", "L1:S5", "audio.out", "GLOBALBUS", "audio.in", PatchGraph::PortType::audio, true };
        layer0.sequence = 1; layer1.sequence = 0;
        if (! graph.addEdge (layer0) || ! graph.addEdge (layer1)
            || ! graph.addEdge ({ "global:master", "GLOBALBUS", "audio.out", "MASTER", "audio.in", PatchGraph::PortType::audio, false }))
            return fail ("routing compiler graph edges were rejected");

        const auto compiled = DspRouting::compile (graph);
        if (! compiled.validation.ok || compiled.plan.layerOrder[0] != 1 || compiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler ignored persisted layer-combine order");
        if (! compiled.plan.parallelFx[0])
            return fail ("routing compiler ignored main FX parallel topology");

        const auto restored = PatchGraph::Document::fromValueTree (graph.toValueTree());
        const auto restoredCompiled = DspRouting::compile (restored);
        if (! restoredCompiled.validation.ok || restoredCompiled.plan.layerOrder[0] != 1 || restoredCompiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler order did not survive graph state round-trip");
        if (! restoredCompiled.plan.parallelFx[0])
            return fail ("parallel FX topology did not survive graph state round-trip");

        DspRouting::AtomicPlan published;
        published.publish (restoredCompiled.plan);
        const auto snapshot = published.snapshot();
        if (snapshot.layerOrder != restoredCompiled.plan.layerOrder || snapshot.parallelFx != restoredCompiled.plan.parallelFx || ! snapshot.graphAuthored)
            return fail ("atomic routing plan publication changed the compiled topology");
    }
    {
        VoiceParameters fullRack;
        fullRack.osc1Wave = 1; fullRack.osc2Mix = 0.0f; fullRack.release = 0.03f;
        const auto dry = OfflineRenderer::renderPatch (fullRack, 22050.0, 0.55f, 220.0f);
        fullRack.globalFxModules[0] = { 1, 0, false, 0.58f, 0.0f, 0.08f, 0.85f };
        const auto globallyFiltered = OfflineRenderer::renderPatch (fullRack, 22050.0, 0.55f, 220.0f);
        if (! finiteAudio (globallyFiltered) || maxDifference (dry, globallyFiltered) < 0.001f)
            return fail ("offline whole-instrument global filter did not process the completed rack");

        VoiceParameters probeDry;
        const auto drySignature = EffectChainProbe::probe (probeDry);
        probeDry.fxModules[0] = { 3, 0, false, 0.72f, 0.45f, 0.50f, 1.0f };
        const auto drivenSignature = EffectChainProbe::probe (probeDry);
        float detailedSum = 0.0f;
        for (const auto value : drivenSignature.detailedBands)
        {
            if (! std::isfinite (value)) return fail ("detailed FX probe contained non-finite values");
            detailedSum += value;
        }
        if (std::abs (detailedSum - 1.0f) > 0.02f) return fail ("detailed FX transfer fingerprint was not normalized");
        if (drivenSignature.nonlinear <= drySignature.nonlinear)
            return fail ("two-tone FX probe did not detect added saturation");

        VoiceParameters probeGlobal;
        const auto globalDrySignature = EffectChainProbe::probe (probeGlobal);
        probeGlobal.globalFxModules[0] = { 3, 0, false, 0.72f, 0.45f, 0.50f, 1.0f };
        const auto globalDrivenSignature = EffectChainProbe::probe (probeGlobal);
        if (globalDrivenSignature.nonlinear <= globalDrySignature.nonlinear)
            return fail ("two-tone FX probe ignored post-sum global saturation");
    }
    {
        SoundFeatures guitar;
        guitar.duration = 1.8f; guitar.fundamentalHz = 110.0f; guitar.pitchConfidence = 0.84f;
        guitar.harmonicity = 0.68f; guitar.inharmonicity = 0.16f; guitar.transientScore = 0.72f;
        guitar.spectralFlatness = 0.12f; guitar.spectralCentroidHz = 1850.0f; guitar.spectralRolloffHz = 6200.0f;
        guitar.lowEnergyRatio = 0.20f; guitar.highEnergyRatio = 0.16f; guitar.zeroCrossingRate = 0.08f;
        guitar.attackSeconds = 0.004f; guitar.decaySeconds = 0.34f; guitar.sustainLevel = 0.46f; guitar.releaseSeconds = 0.52f;
        guitar.stereoWidth = 0.34f; guitar.spectralMotion = 0.08f;
        const auto advice = ResynthesisAdvisor::advise (guitar);
        if (advice.method != 6 || advice.complexity < 1) return fail ("guitar-like reference was not routed to FX / Guitar Chain");
        auto seed = SoundMatcher::initialFit (guitar).params;
        MatchSettings fxSettings; fxSettings.algorithm = 6; fxSettings.iterations = 0; fxSettings.topologyTrials = 0;
        fxSettings.populationSize = 2; fxSettings.renderSampleRate = 12000.0; fxSettings.maxRenderSeconds = 0.5f;
        const auto fxMatch = SoundMatcher::refineFit (guitar, seed, fxSettings);
        if (! std::isfinite (fxMatch.effectProbeSimilarity) || fxMatch.effectProbeSimilarity < 0.0f || fxMatch.effectProbeSimilarity > 1.0f)
            return fail ("FX / Guitar Chain diagnostic probe score invalid");

        SoundFeatures acid;
        acid.duration = 0.65f; acid.fundamentalHz = 110.0f; acid.pitchConfidence = 0.97f;
        acid.harmonicity = 0.94f; acid.inharmonicity = 0.04f; acid.transientScore = 0.64f;
        acid.spectralFlatness = 0.04f; acid.spectralCentroidHz = 2400.0f; acid.spectralRolloffHz = 9000.0f;
        acid.lowEnergyRatio = 0.18f; acid.highEnergyRatio = 0.24f; acid.zeroCrossingRate = 0.10f;
        acid.attackSeconds = 0.002f; acid.decaySeconds = 0.18f; acid.sustainLevel = 0.36f; acid.releaseSeconds = 0.09f;
        acid.stereoWidth = 0.02f; acid.spectralMotion = 0.05f;
        const auto acidAdvice = ResynthesisAdvisor::advise (acid);
        if (acidAdvice.method == 6)
            return fail ("clean acid/303-like reference was misrouted to FX / Guitar Chain");
    }
    {
        SoundFeatures clean;
        clean.duration = 1.0f; clean.fundamentalHz = 220.0f; clean.pitchConfidence = 0.96f;
        clean.harmonicity = 0.88f; clean.inharmonicity = 0.08f; clean.transientScore = 0.56f;
        clean.spectralFlatness = 0.07f; clean.spectralCentroidHz = 2600.0f; clean.spectralRolloffHz = 7600.0f;
        clean.lowEnergyRatio = 0.16f; clean.highEnergyRatio = 0.22f; clean.zeroCrossingRate = 0.08f;
        clean.attackSeconds = 0.008f; clean.decaySeconds = 0.31f; clean.sustainLevel = 0.54f; clean.releaseSeconds = 0.18f;
        clean.stereoWidth = 0.10f; clean.spectralMotion = 0.025f;
        const auto cleanSeed = SoundMatcher::initialFit (clean).params;
        if (cleanSeed.drive > 1.0e-6f || cleanSeed.wavefold > 1.0e-6f)
            return fail ("clean bright reference seed manufactured nonlinear drive/fold");

        MatchSettings cleanFxSettings;
        cleanFxSettings.algorithm = 6; cleanFxSettings.iterations = 0; cleanFxSettings.topologyTrials = 0;
        cleanFxSettings.populationSize = 2; cleanFxSettings.renderSampleRate = 12000.0; cleanFxSettings.maxRenderSeconds = 0.5f;
        const auto cleanFx = SoundMatcher::refineFit (clean, cleanSeed, cleanFxSettings);
        for (const auto& module : cleanFx.params.fxModules)
            if ((module.type == 3 || module.type == 4 || module.type == 5) && ! module.bypass && module.mix > 0.001f)
                return fail ("FX/Guitar profile forced nonlinear processing without sufficient evidence");

        for (int i = 0; i < 48; ++i)
        {
            const auto variation = SoundMatcher::makeVariation (VoiceParameters {}, i);
            if (variation.drive > 1.0e-6f || variation.wavefold > 1.0e-6f)
                return fail ("generic variation invented built-in nonlinear colour from a neutral patch");
            for (const auto& module : variation.fxModules)
                if (module.type == 3 || module.type == 4 || module.type == 5)
                    return fail ("generic topology mutation invented nonlinear FX from a neutral patch");
        }
    }
    {
        VoiceParameters tone; tone.osc1Wave = 1; tone.osc2Mix = 0; tone.release = 0.02f;
        const auto dry = OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220);
        for (int type = 1; type < (int) fxModuleCatalog.size(); ++type)
        {
            tone.fxModules[0] = { type, 0, false, 0.45f, 0.4f, 0.5f, 0.75f };
            // The quiet synth fixture must exceed the compressor threshold.
            if (type == 13) tone.fxModules[0].amount = 1.0f;
            auto rendered = OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220);
            if (! finiteAudio (rendered) || maxDifference (dry, rendered) < 1.0e-5f)
            { std::cerr << "FX type=" << type << " difference=" << maxDifference (dry, rendered) << " finite=" << finiteAudio (rendered) << "\n"; return fail ("FX rack type failed to process audio"); }
            tone.fxModules[0].bypass = true;
            if (maxDifference (dry, OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220)) > 1.0e-6f)
                return fail ("FX module bypass changed audio");
        }
        tone.fxModules[0] = { 1, 0, false, 0.25f, 0.3f, 0.2f, 1.0f };
        tone.fxModules[1] = { 3, 0, false, 0.55f, 0.8f, 0.5f, 1.0f };
        const auto forward = OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220);
        std::swap (tone.fxModules[0], tone.fxModules[1]);
        if (maxDifference (forward, OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220)) < 0.001f)
            return fail ("FX rack ordering had no effect");
        tone.fxModules[1] = {}; tone.drive = 0.6f;
        const auto pre = OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220);
        tone.fxModules[0].stage = 1;
        if (maxDifference (pre, OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220)) < 0.001f)
            return fail ("pre/post FX routing had no effect");

        auto parallelProbe = tone;
        parallelProbe.fxModules[0] = { 1, 0, false, 0.42f, 0.25f, 0.10f, 1.0f };
        parallelProbe.fxModules[1] = { 3, 1, false, 0.52f, 0.55f, 0.40f, 0.85f };
        parallelProbe.drive = 0.48f; parallelProbe.chorusMix = 0.22f; parallelProbe.delayMix = 0.12f;
        DspRouting::Plan serialPlan, parallelPlan;
        parallelPlan.parallelFx[0] = true; parallelPlan.graphAuthored = true;
        const auto serialFx = OfflineRenderer::renderPatch (parallelProbe, 22050, 0.6f, 220, 128, serialPlan);
        const auto parallelFx = OfflineRenderer::renderPatch (parallelProbe, 22050, 0.6f, 220, 128, parallelPlan);
        if (! finiteAudio (parallelFx) || parallelFx.getMagnitude (0, parallelFx.getNumSamples()) <= 1.0e-5f)
            return fail ("parallel FX routing generated invalid or silent audio");
        if (maxDifference (serialFx, parallelFx) < 0.001f)
            return fail ("serial and compensated parallel FX routing sounded identical");
        if (parallelFx.getMagnitude (0, parallelFx.getNumSamples()) > serialFx.getMagnitude (0, serialFx.getNumSamples()) * 3.0f + 0.05f)
            return fail ("parallel FX split/merge produced an unsafe level jump");

        tone.fxModules = {}; tone.drive = 0;
        tone.moduleModSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::amplitude, 0.9f };
        const auto slow = OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220);
        tone.extraLfoRate[0] = 12;
        if (maxDifference (slow, OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220)) < 0.01f)
            return fail ("independent LFO module was not routed");
        bool routes = false, modules = false, envelopes = false;
        for (int i = 0; i < 64; ++i)
        {
            const auto variation = SoundMatcher::makeVariation (VoiceParameters {}, i);
            for (const auto& slot : variation.moduleModSlots) routes |= slot.source != 0 && slot.destination != 0;
            for (const auto& module : variation.fxModules) modules |= module.type != 0;
            envelopes |= variation.mseg.enabled;
        }
        if (! routes || ! modules || ! envelopes) return fail ("matcher failed to explore routes, FX and MSEG topology");
        if (factoryPresetCatalog.size() != 110) return fail ("factory catalog must contain 110 presets");
        bool foundDeepFactoryPreset = false;
        for (int i = 10; i < (int) factoryPresetCatalog.size(); ++i)
        {
            const auto designed = makeFactoryPreset (i); int activeLayers = 0;
            for (const auto& layer : designed.layers) if (layer) ++activeLayers;
            foundDeepFactoryPreset |= activeLayers >= 4;
        }
        if (! foundDeepFactoryPreset) return fail ("factory library must contain genuinely deep multi-layer patches");
        auto combined = makeFactoryPreset (0);
        combined.layers[0] = std::make_shared<VoiceParameters> (makeFactoryPreset (2));
        const auto additive = OfflineRenderer::renderPatch (combined, 22050, 0.5f, 220);
        auto bypass = combined; bypass.layers.fill (nullptr);
        const auto original = OfflineRenderer::renderPatch (bypass, 22050, 0.5f, 220);
        for (int operation = 0; operation < 5; ++operation)
        {
            combined.layerOperation[0] = operation; combined.layerAmount[0] = 0;
            if (maxDifference (original, OfflineRenderer::renderPatch (combined, 22050, 0.5f, 220)) > 1.0e-6f)
                return fail ("zero combination amount must bypass every operation");
            combined.layerAmount[0] = 1;
            const auto processed = OfflineRenderer::renderPatch (combined, 22050, 0.5f, 220);
            if (! finiteAudio (processed)) return fail ("combination generated non-finite audio");
            if (operation > 0 && maxDifference (additive, processed) < 0.001f)
                return fail ("combination mode has no audible effect");
        }
        combined.layerOperation[0] = 4; combined.layerGain[0] = 0;
        const auto dividedBySilence = OfflineRenderer::renderPatch (combined, 22050, 0.5f, 220);
        if (! finiteAudio (dividedBySilence) || dividedBySilence.getMagnitude (0, dividedBySilence.getNumSamples()) > 1)
            return fail ("division by silence must remain bounded");

        auto orderProbe = makeFactoryPreset (0);
        orderProbe.layers.fill (nullptr);
        auto orderLayerA = std::make_shared<VoiceParameters> (makeFactoryPreset (2));
        auto orderLayerB = std::make_shared<VoiceParameters> (makeFactoryPreset (4));
        orderLayerA->layers.fill (nullptr); orderLayerB->layers.fill (nullptr);
        orderProbe.layers[0] = orderLayerA; orderProbe.layers[1] = orderLayerB;
        orderProbe.mainLayerGain = 0.72f;
        orderProbe.layerGain[0] = 0.56f; orderProbe.layerGain[1] = 0.48f;
        orderProbe.layerOperation[0] = 2; orderProbe.layerAmount[0] = 0.88f;
        orderProbe.layerOperation[1] = 3; orderProbe.layerAmount[1] = 0.82f;
        DspRouting::Plan canonicalOrder;
        auto swappedOrder = canonicalOrder;
        std::swap (swappedOrder.layerOrder[0], swappedOrder.layerOrder[1]);
        swappedOrder.graphAuthored = true;
        const auto canonicalAudio = OfflineRenderer::renderPatch (orderProbe, 22050, 0.5f, 220, 128, canonicalOrder);
        const auto reorderedAudio = OfflineRenderer::renderPatch (orderProbe, 22050, 0.5f, 220, 128, swappedOrder);
        if (! finiteAudio (canonicalAudio) || ! finiteAudio (reorderedAudio)
            || canonicalAudio.getMagnitude (0, canonicalAudio.getNumSamples()) < 1.0e-5f
            || reorderedAudio.getMagnitude (0, reorderedAudio.getNumSamples()) < 1.0e-5f)
            return fail ("compiled layer routing produced silent or non-finite audio");
        if (maxDifference (canonicalAudio, reorderedAudio) < 1.0e-4f)
            return fail ("compiled layer-combine order did not create a real sonic change");

        for (int i = 0; i < (int) factoryPresetCatalog.size(); ++i)
        {
            const auto audio = OfflineRenderer::renderPatch (makeFactoryPreset (i), 22050, 0.65f, 220);
            if (! finiteAudio (audio) || audio.getMagnitude (0, audio.getNumSamples()) < 0.001f)
                return fail ("factory preset rendered silence or invalid audio");
        }
    }
    {
        VoiceParameters tone; tone.osc1Wave = 0; tone.osc2Mix = 0; tone.release = 0.02f;
        const auto clean = OfflineRenderer::renderPatch (tone, 22050, 0.5f, 220);
        tone.drive = 0.7f; tone.distortionMix = 0;
        if (maxDifference (clean, OfflineRenderer::renderPatch (tone, 22050, 0.5f, 220)) > 1.0e-6f)
            return fail ("dry distortion mix changed the signal");
        tone.distortionMix = 1;
        auto previous = clean;
        for (int mode = 0; mode < 3; ++mode)
        {
            tone.distortionMode = mode;
            auto rendered = OfflineRenderer::renderPatch (tone, 22050, 0.5f, 220);
            if (! finiteAudio (rendered) || maxDifference (previous, rendered) < 0.001f)
                return fail ("distortion modes are silent, non-finite or identical");
            previous = std::move (rendered);
        }
        tone.drive = 0;
        VoiceParameters layered; layered.mainLayerGain = 0;
        layered.layers[0] = std::make_shared<VoiceParameters> (tone);
        layered.layerGain[0] = 0.5f; layered.layerPan[0] = -1;
        auto audio = OfflineRenderer::renderPatch (layered, 22050, 0.5f, 220);
        if (! finiteAudio (audio) || audio.getMagnitude (0, 0, audio.getNumSamples()) < 0.01f
            || audio.getMagnitude (1, 0, audio.getNumSamples()) > 1.0e-6f)
            return fail ("layer-only MIDI rendering or independent pan failed");
        layered.layerTune[0] = 12;
        auto pitched = OfflineRenderer::renderPatch (layered, 22050, 0.5f, 220);
        if (maxDifference (audio, pitched) < 0.01f) return fail ("layer tuning had no effect");
        layered.layers[1] = layered.layers[0]; layered.layerPan[1] = 1;
        audio = OfflineRenderer::renderPatch (layered, 22050, 0.5f, 220);
        if (audio.getMagnitude (1, 0, audio.getNumSamples()) < 0.01f) return fail ("third synth layer did not render");
        layered.layers.fill (nullptr);
        audio = OfflineRenderer::renderPatch (layered, 22050, 0.5f, 220);
        if (audio.getMagnitude (0, audio.getNumSamples()) > 1.0e-6f) return fail ("cleared layers still rendered");
    }
    // Optional local reference benchmarks; audio remains outside the repository.
    for (int arg = 1; arg < argc; ++arg)
    {
        const juce::File file (juce::String::fromUTF8 (argv[arg]));
        const auto features = SampleAnalyzer::analyzeFile (file);
        if (! features) return fail ("could not read benchmark reference");
        auto seed = SoundMatcher::initialFit (*features).params;
        seed.referenceWavetable = ReferenceWavetableExtractor::extract (file, features->fundamentalHz);
        if (seed.osc1Wave != 0 && seed.referenceWavetable) seed.referenceWavetableMix = 0.32f;
        const auto quick = SoundMatcher::evaluateFit (*features, seed);
        const auto refined = SoundMatcher::refineFit (*features, seed);
        std::cout << file.getFileName() << " Hz=" << features->fundamentalHz
                  << " quick=" << quick.similarity.total << " refined=" << refined.similarity.total << std::endl;
        if (refined.similarity.total < quick.similarity.total) return fail ("reference benchmark regressed");
    }
    for (const double rate : { 44100.0, 48000.0 })
        for (const float hz : { 27.5f, 32.7032f, 41.2034f, 55.0f, 82.4069f, 523.2511f })
        {
            juce::AudioBuffer<float> sine (1, (int) rate);
            for (int i = 0; i < sine.getNumSamples(); ++i)
                sine.setSample (0, i, 0.5f * std::sin ((float) (juce::MathConstants<double>::twoPi * hz * i / rate)));
            const auto features = SampleAnalyzer::analyzeBuffer (sine, rate);
            if (std::abs (1200.0f * std::log2 (features.fundamentalHz / hz)) > 5.0f)
                return fail ("sine pitch detection has an octave or tuning error");
            const auto clean = SoundMatcher::initialFit (features).params;
            const auto rendered = OfflineRenderer::renderPatch (clean, rate, 1.0f, hz);
            const auto measured = SampleAnalyzer::analyzeBuffer (rendered, rate, hz);
            if (measured.spectralRolloffHz > hz * 1.5f || measured.spectralCentroidHz > hz * 1.6f)
                return fail ("clean sine matching introduced audible harmonic layers");
        }
    // Recording pre-roll must not become the analyzed timbre or a normalized
    // noise frame. A non-integer cycle length also probes table seam distortion.
    juce::TemporaryFile recording (".wav");
    constexpr int recordingRate = 44100, recordingSamples = recordingRate * 2;
    constexpr float recordingHz = 1046.5023f;
    {
        juce::FileOutputStream stream (recording.getFile());
        if (! stream.openedOk()) return fail ("could not create pre-roll fixture");
        stream.write ("RIFF", 4); stream.writeInt (36 + recordingSamples * 2);
        stream.write ("WAVEfmt ", 8); stream.writeInt (16);
        stream.writeShort (1); stream.writeShort (1); stream.writeInt (recordingRate);
        stream.writeInt (recordingRate * 2); stream.writeShort (2); stream.writeShort (16);
        stream.write ("data", 4); stream.writeInt (recordingSamples * 2);
        for (int i = 0; i < recordingSamples; ++i)
            stream.writeShort (i < recordingRate ? 0 : (short) (16000.0 * std::sin (
                juce::MathConstants<double>::twoPi * recordingHz * (i - recordingRate) / recordingRate)));
    }
    const auto recordingFeatures = SampleAnalyzer::analyzeFile (recording.getFile());
    if (ReferenceWavetableExtractor::extract (recording.getFile(), recordingHz, 0.0, 0.8))
        return fail ("wavetable extraction ignored the region end and read later audio");
    if (ReferenceWavetableExtractor::extract (recording.getFile(), recordingHz, 1.99, 1.8)
        || ReferenceWavetableExtractor::extract (recording.getFile(), recordingHz, 2.0, 2.0)
        || ReferenceWavetableExtractor::extract (recording.getFile(), recordingHz, 1.5, 1.50001))
        return fail ("invalid or too-short wavetable region was accepted");
    if (! ReferenceWavetableExtractor::extract (recording.getFile(), recordingHz, 1.2, 1.8))
        return fail ("audible sample selection did not produce a wavetable");
    if (! ReferenceWavetableExtractor::chop (recording.getFile(), 1.1, 1.9)
        || ReferenceWavetableExtractor::chop (recording.getFile(), 0.1, 0.9))
        return fail ("sample chopping accepted silence or rejected five audible slices");
    if (! recordingFeatures || recordingFeatures->duration > 1.02f
        || std::abs (recordingFeatures->fundamentalHz - recordingHz) > 2.0f
        || recordingFeatures->spectralCentroidHz > recordingHz * 1.1f)
        return fail ("recording pre-roll corrupted analysis");
    const auto extracted = ReferenceWavetableExtractor::extract (recording.getFile(), recordingHz);
    if (! extracted || ! extracted->valid) return fail ("pre-roll wavetable extraction failed");
    for (const auto& frame : extracted->frames)
    {
        double sine = 0.0, cosine = 0.0, energy = 0.0;
        for (int i = 0; i < ReferenceWavetableData::tableSize; ++i)
        {
            const double phase = juce::MathConstants<double>::twoPi * i / ReferenceWavetableData::tableSize;
            sine += frame[(size_t) i] * std::sin (phase);
            cosine += frame[(size_t) i] * std::cos (phase);
            energy += frame[(size_t) i] * frame[(size_t) i];
        }
        const double fundamentalEnergy = 2.0 * (sine * sine + cosine * cosine) / ReferenceWavetableData::tableSize;
        if (energy < 1.0 || fundamentalEnergy / energy < 0.999)
            return fail ("fractional-cycle extraction distorted a sine");
    }
    VoiceParameters longProbe;
    const auto longRender = OfflineRenderer::renderPatch (longProbe, 22050.0, 5.25f, 110.0f);
    if (longRender.getNumSamples() != (int) std::ceil (22050.0 * 5.25))
        return fail ("offline renderer truncated a long reference");
    // v1.0 reference wavetable state round-trip.
    auto wt = std::make_shared<ReferenceWavetableData>();
    wt->valid = true; wt->fundamentalHz = 220.0f;
    for (int f = 0; f < ReferenceWavetableData::frameCount; ++f)
        for (int i = 0; i < ReferenceWavetableData::tableSize; ++i)
            wt->frames[(size_t) f][(size_t) i] = std::sin (juce::MathConstants<double>::twoPi * i / ReferenceWavetableData::tableSize) * (1.0f - f * 0.08f);
    auto restoredWt = ReferenceWavetableData::fromBase64 (wt->toBase64());
    if (! restoredWt || ! restoredWt->valid || std::abs (restoredWt->sample (0.25, 0.5f) - wt->sample (0.25, 0.5f)) > 1.0e-4f)
    { std::cerr << "Reference wavetable serialization failed\n"; return 20; }
    constexpr double sampleRate = 44100.0;
    constexpr float fundamental = 261.6256f;

    // Direct MSEG lifecycle probe at a small, exact sample rate. It must remain
    // active while a loop is held, stay finite/in-range, preserve the current
    // value at release, and then reach the final point and become inactive.
    MsegParameters msegParameters;
    msegParameters.enabled = true;
    msegParameters.loopEnabled = true;
    msegParameters.loopStartPoint = 1;
    msegParameters.loopEndPoint = 3;
    msegParameters.levels = {{ 0.0f, 1.0f, 0.24f, 0.72f, 0.18f, 0.0f }};
    msegParameters.times = {{ 0.010f, 0.012f, 0.014f, 0.016f, 0.018f }};
    msegParameters.curves = {{ 0.0f, 0.35f, -0.30f, 0.15f, -0.20f }};

    MultiSegmentEnvelope msegProbe;
    msegProbe.setSampleRate (1000.0);
    msegProbe.setParameters (msegParameters);
    msegProbe.noteOn();

    float heldMin = 1.0f, heldMax = 0.0f;
    for (int i = 0; i < 180; ++i)
    {
        const float value = msegProbe.getNextSample();
        if (! std::isfinite (value) || value < -1.0e-5f || value > 1.00001f)
            return fail ("MSEG generated an invalid held value");
        heldMin = std::min (heldMin, value);
        heldMax = std::max (heldMax, value);
    }
    if (! msegProbe.isActive())
        return fail ("MSEG loop stopped while the note was still held");
    if (heldMax < 0.90f || heldMin > 0.35f)
        return fail ("MSEG loop did not traverse the configured levels");

    const float beforeRelease = msegProbe.getCurrentValue();
    msegProbe.noteOff();
    const float firstRelease = msegProbe.getNextSample();
    if (std::abs (firstRelease - beforeRelease) > 1.0e-5f)
        return fail ("MSEG release introduced a discontinuity");

    for (int i = 0; i < 200 && msegProbe.isActive(); ++i)
    {
        const float value = msegProbe.getNextSample();
        if (! std::isfinite (value)) return fail ("MSEG release generated a non-finite value");
    }
    if (msegProbe.isActive())
        return fail ("MSEG did not complete after note release");
    if (std::abs (msegProbe.getCurrentValue() - msegParameters.levels.back()) > 1.0e-5f)
        return fail ("MSEG did not finish at its final point");

    VoiceParameters target;
    target.osc1Wave = 1;
    target.osc2Wave = 2;
    target.osc1Mix = 0.72f;
    target.osc2Mix = 0.25f;
    target.osc2Detune = 8.0f;
    target.cutoff = 5200.0f;
    target.resonance = 0.26f;
    target.attack = 0.015f;
    target.decay = 0.22f;
    target.sustain = 0.66f;
    target.release = 0.20f;
    target.wavetableMix = 0.18f;
    target.wavetablePosition = 0.68f;
    target.wavetableWarp = -0.16f;
    target.supersawMix = 0.22f;
    target.unisonDetune = 21.0f;
    target.unisonSpread = 0.82f;
    target.wavefold = 0.12f;
    target.fmMix = 0.24f;
    target.fmAlgorithm = 2;
    target.fmFeedback = 0.12f;
    target.fmOpRatio = {{ 1.0f, 2.0f, 3.0f, 1.5f, 4.0f, 2.0f }};
    target.fmOpLevel = {{ 1.0f, 0.48f, 0.28f, 0.34f, 0.20f, 0.12f }};
    target.fmOpFixedMode[5] = 1;
    target.fmOpFixedHz[5] = 1180.0f;
    target.fmOpAttack = {{ 0.008f, 0.003f, 0.002f, 0.004f, 0.006f, 0.001f }};
    target.fmOpDecay = {{ 0.50f, 0.24f, 0.13f, 0.31f, 0.18f, 0.09f }};
    target.fmOpSustain = {{ 0.92f, 0.62f, 0.34f, 0.52f, 0.22f, 0.08f }};
    target.fmOpRelease = {{ 0.30f, 0.18f, 0.11f, 0.24f, 0.15f, 0.07f }};
    target.fmOpKeyScale[4] = 0.35f;
    target.fmOpVelocity[2] = 0.78f;
    target.modSlots[0] = { (int) ModSource::lfo1, (int) ModDestination::cutoff, 0.12f };
    target.modSlots[1] = { (int) ModSource::ampEnvelope, (int) ModDestination::wavetablePosition, 0.18f };
    target.lfoRate = 0.72f;
    target.chorusMix = 0.12f;
    target.stereoWidth = 1.2f;

    // Matching compares multiple offline renders of the same candidate. Noise and
    // random-note modulation must therefore be reproducible for a stable score.
    auto deterministicProbe = target;
    deterministicProbe.noiseMix = 0.12f;
    deterministicProbe.modSlots[2] = { (int) ModSource::randomNote, (int) ModDestination::cutoff, 0.08f };
    const auto probeA = OfflineRenderer::renderPatch (deterministicProbe, sampleRate, 0.5f, fundamental, 128);
    const auto probeB = OfflineRenderer::renderPatch (deterministicProbe, sampleRate, 0.5f, fundamental, 128);
    if (maxDifference (probeA, probeB) > 1.0e-7f)
        return fail ("offline renderer is not deterministic");

    // User wavetable import converts arbitrary multi-frame source audio into the
    // immutable internal 5x2048 representation. Use eight deliberately different
    // 1024-sample frames so both frame interpolation and rendered timbre are tested.
    constexpr int sourceFrameSize = 1024;
    constexpr int sourceFrameCount = 8;
    juce::AudioBuffer<float> sourceTable (2, sourceFrameSize * sourceFrameCount);
    for (int frame = 0; frame < sourceFrameCount; ++frame)
    {
        for (int i = 0; i < sourceFrameSize; ++i)
        {
            const float phase = (float) i / (float) sourceFrameSize;
            const float angle = juce::MathConstants<float>::twoPi * phase;
            const float harmonic = 1.0f + (float) frame * 0.45f;
            const float value = 0.62f * std::sin (angle)
                              + (0.08f + 0.045f * frame) * std::sin (angle * (2.0f + harmonic))
                              + 0.06f * std::sin (angle * (5.0f + frame));
            sourceTable.setSample (0, frame * sourceFrameSize + i, value);
            sourceTable.setSample (1, frame * sourceFrameSize + i, value * (0.92f - 0.03f * frame));
        }
    }

    juce::String importedDescription;
    auto importedTable = ReferenceWavetableExtractor::importSetFromBuffer (
        sourceTable, 48000.0, sourceFrameSize, &importedDescription);
    if (importedTable == nullptr || ! importedTable->valid)
        return fail ("user wavetable buffer import failed");
    if (! importedDescription.contains ("8 source frames") || ! importedDescription.contains ("1024"))
        return fail ("user wavetable import metadata is incorrect");
    if (std::abs (importedTable->sample (0.21, 0.0f) - importedTable->sample (0.21, 1.0f)) <= 1.0e-4f)
        return fail ("user wavetable import collapsed frame evolution");

    auto restoredUserTable = ReferenceWavetableData::fromBase64 (importedTable->toBase64());
    if (restoredUserTable == nullptr || ! restoredUserTable->valid
        || std::abs (restoredUserTable->sample (0.37, 0.73f) - importedTable->sample (0.37, 0.73f)) > 1.0e-4f)
        return fail ("user wavetable serialization round-trip failed");

    auto userWavetableProbe = target;
    userWavetableProbe.osc1Mix = 0.12f;
    userWavetableProbe.osc2Mix = 0.0f;
    userWavetableProbe.wavetableMix = 0.0f;
    userWavetableProbe.referenceWavetableMix = 0.0f;
    userWavetableProbe.supersawMix = 0.0f;
    userWavetableProbe.fmMix = 0.0f;
    userWavetableProbe.chorusMix = 0.0f;
    userWavetableProbe.delayMix = 0.0f;
    userWavetableProbe.reverbMix = 0.0f;
    userWavetableProbe.userWavetable = importedTable;
    userWavetableProbe.userWavetableMix = 0.82f;
    const auto userWtA = OfflineRenderer::renderPatch (userWavetableProbe, sampleRate, 0.52f, fundamental, 128);
    const auto userWtB = OfflineRenderer::renderPatch (userWavetableProbe, sampleRate, 0.52f, fundamental, 128);
    auto userWtDisabled = userWavetableProbe;
    userWtDisabled.userWavetableMix = 0.0f;
    const auto userWtDry = OfflineRenderer::renderPatch (userWtDisabled, sampleRate, 0.52f, fundamental, 128);
    if (! finiteAudio (userWtA) || userWtA.getMagnitude (0, userWtA.getNumSamples()) <= 1.0e-5f)
        return fail ("user wavetable generated invalid or silent audio");
    if (maxDifference (userWtA, userWtB) > 1.0e-7f)
        return fail ("user wavetable render is not deterministic");
    if (maxDifference (userWtA, userWtDry) <= 1.0e-5f)
        return fail ("user wavetable mix did not alter rendered audio");

    // The append-only graph must reach the actual synthesis engine. Use a strong
    // MSEG->cutoff route, verify deterministic output, and verify the route changes
    // the rendered result compared with the identical patch with MSEG disabled.
    auto msegRenderProbe = target;
    msegRenderProbe.chorusMix = 0.0f;
    msegRenderProbe.delayMix = 0.0f;
    msegRenderProbe.reverbMix = 0.0f;
    msegRenderProbe.cutoff = 2400.0f;
    msegRenderProbe.mseg.enabled = true;
    msegRenderProbe.mseg.loopEnabled = true;
    msegRenderProbe.mseg.loopStartPoint = 1;
    msegRenderProbe.mseg.loopEndPoint = 4;
    msegRenderProbe.mseg.levels = {{ 0.0f, 1.0f, 0.12f, 0.92f, 0.28f, 0.0f }};
    msegRenderProbe.mseg.times = {{ 0.015f, 0.050f, 0.075f, 0.060f, 0.120f }};
    msegRenderProbe.mseg.curves = {{ 0.25f, -0.20f, 0.35f, -0.30f, 0.0f }};
    msegRenderProbe.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff, 0.88f };

    const auto msegRenderA = OfflineRenderer::renderPatch (msegRenderProbe, sampleRate, 0.62f, fundamental, 128);
    const auto msegRenderB = OfflineRenderer::renderPatch (msegRenderProbe, sampleRate, 0.62f, fundamental, 128);
    auto msegDisabledProbe = msegRenderProbe;
    msegDisabledProbe.mseg.enabled = false;
    const auto msegDisabledRender = OfflineRenderer::renderPatch (msegDisabledProbe, sampleRate, 0.62f, fundamental, 128);

    if (! finiteAudio (msegRenderA) || msegRenderA.getMagnitude (0, msegRenderA.getNumSamples()) <= 1.0e-5f)
        return fail ("MSEG graph generated invalid or silent audio");
    if (maxDifference (msegRenderA, msegRenderB) > 1.0e-7f)
        return fail ("MSEG graph render is not deterministic");
    if (maxDifference (msegRenderA, msegDisabledRender) <= 1.0e-5f)
        return fail ("MSEG modulation graph did not alter rendered audio");

    // Nonlinear quality modes must all remain finite/deterministic, while the
    // oversampled modes must actually alter the nonlinear result rather than being
    // a UI-only switch. Fixed engine latency keeps every mode sample-aligned.
    auto nonlinearProbe = target;
    nonlinearProbe.noiseMix = 0.0f;
    nonlinearProbe.chorusMix = 0.0f;
    nonlinearProbe.delayMix = 0.0f;
    nonlinearProbe.reverbMix = 0.0f;
    nonlinearProbe.wavefold = 0.78f;
    nonlinearProbe.drive = 0.86f;
    nonlinearProbe.cutoff = 17500.0f;

    nonlinearProbe.oversamplingQuality = 0;
    const auto quality1x = OfflineRenderer::renderPatch (nonlinearProbe, sampleRate, 0.42f, fundamental, 128);
    nonlinearProbe.oversamplingQuality = 1;
    const auto quality2x = OfflineRenderer::renderPatch (nonlinearProbe, sampleRate, 0.42f, fundamental, 128);
    nonlinearProbe.oversamplingQuality = 2;
    const auto quality4xA = OfflineRenderer::renderPatch (nonlinearProbe, sampleRate, 0.42f, fundamental, 128);
    const auto quality4xB = OfflineRenderer::renderPatch (nonlinearProbe, sampleRate, 0.42f, fundamental, 128);

    if (! finiteAudio (quality1x) || ! finiteAudio (quality2x) || ! finiteAudio (quality4xA))
        return fail ("oversampling generated non-finite audio");
    if (quality1x.getMagnitude (0, quality1x.getNumSamples()) <= 1.0e-5f
        || quality2x.getMagnitude (0, quality2x.getNumSamples()) <= 1.0e-5f
        || quality4xA.getMagnitude (0, quality4xA.getNumSamples()) <= 1.0e-5f)
        return fail ("oversampling quality mode generated silence");
    if (maxDifference (quality4xA, quality4xB) > 1.0e-7f)
        return fail ("4x oversampling render is not deterministic");
    if (maxDifference (quality1x, quality4xA) <= 1.0e-6f)
        return fail ("oversampling quality switch did not alter nonlinear rendering");

    SynthEngine latencyProbe;
    latencyProbe.prepare (sampleRate, 128, 2);
    if (latencyProbe.getLatencySamples() <= 0)
        return fail ("oversampling engine did not report its fixed latency");

    auto referenceAudio = OfflineRenderer::renderPatch (target, sampleRate, 0.9f, fundamental, 128);
    const auto reference = SampleAnalyzer::analyzeBuffer (referenceAudio, sampleRate, fundamental);

    if (! finiteFeatures (reference)) return fail ("reference analysis generated non-finite descriptors");
    if (reference.rms <= 1.0e-5f) return fail ("offline renderer generated silence");
    if (reference.stereoWidth <= 1.0e-4f) return fail ("supersaw/unison renderer did not create a stereo image");
    if (std::abs (reference.fundamentalHz - fundamental) > 0.01f)
        return fail ("expected reference fundamental was not retained by analysis");

    // A user's manual base-note correction must replace the pitch assumption used
    // by harmonic descriptors and matching, rather than changing only the UI label.
    constexpr float correctedFundamental = 329.6276f;
    const auto correctedReference = SampleAnalyzer::analyzeBuffer (referenceAudio, sampleRate, correctedFundamental);
    if (std::abs (correctedReference.fundamentalHz - correctedFundamental) > 0.01f)
        return fail ("manual reference base-note override was ignored by analysis");
    if (! std::isfinite (correctedReference.pitchConfidence))
        return fail ("manual reference base-note confidence is not finite");

    float temporalEnergy = 0.0f;
    for (const auto& frame : reference.temporalSpectralBands)
        for (const auto value : frame) temporalEnergy += value;
    if (temporalEnergy <= 0.01f) return fail ("time-varying spectral descriptor is empty");

    auto seed = SoundMatcher::initialFit (reference);
    seed.params.oversamplingQuality = 2;
    MatchSettings settings;
    settings.iterations = 10;
    settings.topologyTrials = 3;
    settings.populationSize = 3;
    settings.maxRenderSeconds = 0.9f;

    const auto quick = SoundMatcher::evaluateFit (reference, seed.params, settings);
    const auto refined = SoundMatcher::refineFit (reference, seed.params, settings);

    if (! std::isfinite (quick.similarity.total) || ! std::isfinite (refined.similarity.total))
        return fail ("matcher produced a non-finite score");
    if (quick.params.oversamplingQuality != 2 || refined.params.oversamplingQuality != 2)
        return fail ("matcher changed the render-quality preference");
    if (refined.similarity.total + 1.0e-6f < quick.similarity.total)
    {
        std::cerr << "FAILED: population optimizer regressed below its seed candidate; quick="
                  << quick.similarity.total << " refined=" << refined.similarity.total << '\n';
        return 1;
    }
    if (refined.evaluatedCandidates < 2)
        return fail ("optimizer did not evaluate a candidate population");

    std::cout << "RetroMatch smoke tests passed. quick=" << quick.similarity.total
              << " refined=" << refined.similarity.total
              << " candidates=" << refined.evaluatedCandidates
              << " latency=" << latencyProbe.getLatencySamples() << " samples\n";
    {
        PatchGraph::Document graph;
        PatchGraph::Node source; source.id = "SRC"; source.type = PatchGraph::NodeType::source; source.ports = { PatchGraph::audioOutput() };
        source.position = { 11.0f, 22.0f }; source.positionValid = true;
        PatchGraph::Node filter; filter.id = "FILTER"; filter.type = PatchGraph::NodeType::processor;
        filter.ports = { PatchGraph::audioInput(), PatchGraph::audioOutput(), PatchGraph::modulationInput() };
        PatchGraph::Node master; master.id = "MASTER"; master.type = PatchGraph::NodeType::master; master.ports = { PatchGraph::audioInput() };
        if (! graph.addNode (source) || ! graph.addNode (filter) || ! graph.addNode (master)) return fail ("typed patch graph rejected valid nodes");
        PatchGraph::Edge a; a.id = "a"; a.fromNode = "SRC"; a.fromPort = "audio.out"; a.toNode = "FILTER"; a.toPort = "audio.in"; a.type = PatchGraph::PortType::audio;
        PatchGraph::Edge b; b.id = "b"; b.fromNode = "FILTER"; b.fromPort = "audio.out"; b.toNode = "MASTER"; b.toPort = "audio.in"; b.type = PatchGraph::PortType::audio;
        if (! graph.addEdge (a) || ! graph.addEdge (b) || ! graph.validate().ok) return fail ("typed patch graph rejected valid audio chain");
        const auto order = graph.topologicalOrder();
        if (order.size() != 3 || order[0] != "SRC" || order[1] != "FILTER" || order[2] != "MASTER")
            return fail ("patch graph topological order is not deterministic");
        PatchGraph::Edge wrong = a; wrong.id = "wrong"; wrong.toPort = "mod.in";
        if (graph.validateConnection (wrong).ok) return fail ("patch graph allowed incompatible port types");
        juce::String mutationReason;
        if (graph.removeEdge ("b", &mutationReason)) return fail ("patch graph removed a required audio route that orphaned MASTER");
        if (graph.findEdge ("b") == nullptr) return fail ("failed graph mutation changed the original document");

        PatchGraph::Document editable;
        PatchGraph::Node mod; mod.id = "MOD"; mod.type = PatchGraph::NodeType::modulationRouter; mod.ports = { PatchGraph::modulationOutput() };
        PatchGraph::Node targetA; targetA.id = "A"; targetA.type = PatchGraph::NodeType::processor; targetA.ports = { PatchGraph::modulationInput() };
        PatchGraph::Node targetB = targetA; targetB.id = "B";
        if (! editable.addNode (mod) || ! editable.addNode (targetA) || ! editable.addNode (targetB)) return fail ("editable graph fixture setup failed");
        PatchGraph::Edge route; route.id = "route"; route.fromNode = "MOD"; route.fromPort = "mod.out"; route.toNode = "A"; route.toPort = "mod.in";
        route.type = PatchGraph::PortType::modulation; route.editable = true;
        if (! editable.addEdge (route)) return fail ("editable route fixture rejected valid route");
        auto reconnected = route; reconnected.toNode = "B";
        if (! editable.replaceEdge ("route", reconnected, &mutationReason)) return fail ("editable route endpoint could not be reconnected");
        if (editable.findEdge ("route") == nullptr || editable.findEdge ("route")->toNode != "B") return fail ("reconnected route did not replace the endpoint");
        if (! editable.removeEdge ("route", &mutationReason) || editable.findEdge ("route") != nullptr) return fail ("editable route could not be removed");

        PatchGraph::Document cycle;
        PatchGraph::Node x; x.id = "A"; x.type = PatchGraph::NodeType::processor; x.ports = { PatchGraph::audioInput (true), PatchGraph::audioOutput() };
        PatchGraph::Node y = x; y.id = "B";
        if (! cycle.addNode (x) || ! cycle.addNode (y)) return fail ("cycle fixture node setup failed");
        PatchGraph::Edge ab; ab.id = "ab"; ab.fromNode = "A"; ab.fromPort = "audio.out"; ab.toNode = "B"; ab.toPort = "audio.in"; ab.type = PatchGraph::PortType::audio;
        PatchGraph::Edge ba; ba.id = "ba"; ba.fromNode = "B"; ba.fromPort = "audio.out"; ba.toNode = "A"; ba.toPort = "audio.in"; ba.type = PatchGraph::PortType::audio;
        if (! cycle.addEdge (ab) || cycle.validateConnection (ba).ok) return fail ("patch graph allowed a zero-delay audio cycle");

        graph.view.pan = { 33.0f, -17.0f }; graph.view.zoom = 1.37f; graph.view.snapToGrid = false;
        const auto roundTrip = PatchGraph::Document::fromValueTree (graph.toValueTree());
        const auto* restored = roundTrip.findNode ("SRC");
        if (restored == nullptr || ! restored->positionValid || std::abs (restored->position.x - 11.0f) > 0.001f
            || std::abs (roundTrip.view.zoom - 1.37f) > 0.001f || roundTrip.view.snapToGrid)
            return fail ("patch graph session round-trip lost layout or viewport state");
    }
    return 0;
}
