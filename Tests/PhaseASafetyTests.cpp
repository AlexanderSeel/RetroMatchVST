#include <JuceHeader.h>

#include "ReferenceFixtureCorpus.h"
#include "../Source/Analysis/SampleAnalyzer.h"
#include "../Source/Matching/GeneratedRackGainPolicy.h"
#include "../Source/Matching/OfflineRenderer.h"
#include "../Source/Matching/RenderTelemetry.h"

#include <cmath>
#include <iostream>

namespace
{
int fail (const char* message)
{
    std::cerr << "Phase A safety test failed: " << message << '\n';
    return 1;
}

float windowRms (const juce::AudioBuffer<float>& audio, float startFraction, float endFraction)
{
    const int start = juce::jlimit (0, audio.getNumSamples(), (int) std::floor (audio.getNumSamples() * startFraction));
    const int end = juce::jlimit (start + 1, audio.getNumSamples(), (int) std::ceil (audio.getNumSamples() * endFraction));
    double sumSquares = 0.0;
    std::int64_t count = 0;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = start; i < end; ++i)
        {
            const double sample = audio.getSample (ch, i);
            sumSquares += sample * sample;
            ++count;
        }
    return count > 0 ? (float) std::sqrt (sumSquares / count) : 0.0f;
}

float stereoDifferenceRms (const juce::AudioBuffer<float>& audio)
{
    if (audio.getNumChannels() < 2) return 0.0f;
    double sumSquares = 0.0;
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const double difference = audio.getSample (0, i) - audio.getSample (1, i);
        sumSquares += difference * difference;
    }
    return audio.getNumSamples() > 0 ? (float) std::sqrt (sumSquares / audio.getNumSamples()) : 0.0f;
}

float harmonicMagnitude (const juce::AudioBuffer<float>& audio, double sampleRate, float frequency,
                         float startSeconds, float durationSeconds)
{
    const int start = juce::jlimit (0, audio.getNumSamples() - 1, (int) std::lround (startSeconds * sampleRate));
    const int count = juce::jlimit (1, audio.getNumSamples() - start, (int) std::lround (durationSeconds * sampleRate));
    double real = 0.0;
    double imag = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * frequency * i / sampleRate;
        const double sample = audio.getSample (0, start + i);
        real += sample * std::cos (phase);
        imag -= sample * std::sin (phase);
    }
    return (float) (2.0 * std::sqrt (real * real + imag * imag) / count);
}

VoiceParameters makeCleanSinePatch()
{
    VoiceParameters p;
    p.osc1Mix = 0.45f;
    p.osc2Mix = 0.0f;
    p.subMix = 0.0f;
    p.noiseMix = 0.0f;
    p.ringMix = 0.0f;
    p.additiveMix = 0.0f;
    p.osc1Wave = 0;
    p.wavetableMix = 0.0f;
    p.referenceWavetableMix = 0.0f;
    p.userWavetableMix = 0.0f;
    p.supersawMix = 0.0f;
    p.wavefold = 0.0f;
    p.fmAmount = 0.0f;
    p.fmMix = 0.0f;
    p.drive = 0.0f;
    p.distortionMix = 0.0f;
    p.chorusMix = 0.0f;
    p.delayMix = 0.0f;
    p.reverbMix = 0.0f;
    p.cutoff = 20000.0f;
    p.resonance = 0.0f;
    p.attack = 0.005f;
    p.decay = 0.030f;
    p.sustain = 0.72f;
    p.release = 0.050f;
    p.mseg.enabled = false;
    p.outputGainDb = -6.0f;
    p.mainLayerGain = 1.0f;
    for (auto& module : p.fxModules) module.enabled = false;
    for (auto& module : p.globalFxModules) module.enabled = false;
    return p;
}
}

int main()
{
    constexpr double sampleRate = 48000.0;

    for (const auto& fixture : ReferenceFixtureCorpus::definitions())
    {
        const auto first = ReferenceFixtureCorpus::render (fixture, sampleRate);
        const auto second = ReferenceFixtureCorpus::render (fixture, sampleRate);
        if (first.getNumChannels() != 2 || first.getNumSamples() <= 0
            || first.getNumSamples() != second.getNumSamples())
            return fail ("fixture dimensions are invalid");

        for (int ch = 0; ch < first.getNumChannels(); ++ch)
            for (int i = 0; i < first.getNumSamples(); ++i)
                if (first.getSample (ch, i) != second.getSample (ch, i))
                    return fail ("fixture generation is not deterministic");

        const auto telemetry = RenderTelemetry::analyze (first);
        if (! telemetry.isTechnicallySafe() || telemetry.peak < 0.005f || telemetry.peak > 0.951f)
            return fail ("fixture corpus contains silent, clipped or non-finite audio");

        const auto& expected = fixture.expected;
        if (expected.maxFundamentalHz > 0.0f && expected.maxFundamentalHz < expected.minFundamentalHz)
            return fail ("fixture pitch expectation is malformed");

        const float body = windowRms (first, 0.15f, 0.45f);
        const float tail = windowRms (first, 0.86f, 0.99f);
        if ((expected.lifecycle == ReferenceFixtureCorpus::Lifecycle::oneShot
             || expected.lifecycle == ReferenceFixtureCorpus::Lifecycle::plucked
             || expected.lifecycle == ReferenceFixtureCorpus::Lifecycle::gated)
            && tail > body * 0.42f + 0.002f)
            return fail ("decaying fixture does not audibly terminate");
        if ((expected.lifecycle == ReferenceFixtureCorpus::Lifecycle::sustained
             || expected.lifecycle == ReferenceFixtureCorpus::Lifecycle::evolving)
            && tail < body * 0.08f)
            return fail ("sustained/evolving fixture loses its body before the tail window");

        const float stereoDifference = stereoDifferenceRms (first);
        if (expected.stereo == ReferenceFixtureCorpus::StereoCharacter::mono && stereoDifference > 1.0e-6f)
            return fail ("mono fixture is not mono");
        if (expected.stereo == ReferenceFixtureCorpus::StereoCharacter::wide && stereoDifference < 0.004f)
            return fail ("wide fixture does not contain deterministic stereo information");

        const float expectedPitch = expected.minFundamentalHz > 20.0f
            ? 0.5f * (expected.minFundamentalHz + expected.maxFundamentalHz) : 0.0f;
        const auto features = SampleAnalyzer::analyzeBuffer (first, sampleRate, expectedPitch);
        if (! std::isfinite (features.rms) || ! std::isfinite (features.peak)
            || ! std::isfinite (features.spectralCentroidHz) || ! std::isfinite (features.transientScore))
            return fail ("analyzer produced non-finite fixture features");
        if (expectedPitch > 20.0f && features.pitchConfidence < expected.minPitchConfidence)
            return fail ("pitched fixture does not meet its stored coarse pitch-confidence expectation");
    }

    const auto cleanPatch = makeCleanSinePatch();
    const auto clean = OfflineRenderer::renderPatch (cleanPatch, sampleRate, 1.40f, 220.0f, 256, {}, true);
    const auto cleanTelemetry = RenderTelemetry::analyze (clean);
    if (! cleanTelemetry.isTechnicallySafe() || cleanTelemetry.peak > 0.80f || cleanTelemetry.rms <= 0.01f)
        return fail ("clean sine path is not finite, unclipped and headroom-safe");

    const float fundamental = harmonicMagnitude (clean, sampleRate, 220.0f, 0.55f, 0.50f);
    float upperHarmonics = 0.0f;
    for (int harmonic = 2; harmonic <= 6; ++harmonic)
        upperHarmonics += harmonicMagnitude (clean, sampleRate, 220.0f * harmonic, 0.55f, 0.50f);
    if (fundamental <= 0.01f || upperHarmonics / fundamental > 0.012f)
        return fail ("clean sine path introduces unintended nonlinear harmonic energy");

    auto singleVoice = cleanPatch;
    const float singleScale = GeneratedRackGainPolicy::apply (singleVoice);
    if (std::abs (singleScale - 1.0f) > 1.0e-6f || std::abs (singleVoice.mainLayerGain - 1.0f) > 1.0e-6f)
        return fail ("generated rack policy changed a single-voice patch");

    auto generatedRack = cleanPatch;
    generatedRack.mainLayerGain = 0.78f;
    const std::array<float, VoiceParameters::extraLayerCount> gains {{ 0.30f, 0.18f, 0.24f, 0.20f, 0.17f, 0.16f, 0.13f }};
    for (size_t i = 0; i < generatedRack.layers.size(); ++i)
    {
        auto companion = cleanPatch;
        companion.mainLayerGain = 1.0f;
        generatedRack.layers[i] = std::make_shared<VoiceParameters> (std::move (companion));
        generatedRack.layerGain[i] = gains[i];
        generatedRack.layerAmount[i] = 0.78f;
        generatedRack.layerOperation[i] = 0;
    }

    const float mainBefore = generatedRack.mainLayerGain;
    const auto gainsBefore = generatedRack.layerGain;
    const float contributionBefore = GeneratedRackGainPolicy::coherentContribution (generatedRack);
    const float rackScale = GeneratedRackGainPolicy::apply (generatedRack);
    const float contributionAfter = GeneratedRackGainPolicy::coherentContribution (generatedRack);
    if (contributionBefore <= GeneratedRackGainPolicy::defaultCoherentBudget || rackScale >= 1.0f
        || contributionAfter > GeneratedRackGainPolicy::defaultCoherentBudget + 1.0e-5f)
        return fail ("generated additive rack was not normalized to the coherent gain budget");
    if (std::abs (generatedRack.mainLayerGain / mainBefore - rackScale) > 1.0e-5f)
        return fail ("rack normalization did not preserve main-layer ratio");
    for (size_t i = 0; i < generatedRack.layers.size(); ++i)
        if (std::abs (generatedRack.layerGain[i] / gainsBefore[i] - rackScale) > 1.0e-5f)
            return fail ("rack normalization did not preserve companion-layer ratios");

    const auto layered = OfflineRenderer::renderPatch (generatedRack, sampleRate, 1.40f, 220.0f, 256, {}, true);
    const auto layeredTelemetry = RenderTelemetry::analyze (layered);
    if (! layeredTelemetry.isTechnicallySafe() || layeredTelemetry.peak > 0.98f || layeredTelemetry.rms <= 0.01f)
        return fail ("gain-budgeted additive rack is not rendered headroom-safe");

    auto nonAdditive = cleanPatch;
    nonAdditive.layers[0] = std::make_shared<VoiceParameters> (cleanPatch);
    nonAdditive.layerGain[0] = 1.0f;
    nonAdditive.layerAmount[0] = 1.0f;
    nonAdditive.layerOperation[0] = 1;
    const float nonAdditiveScale = GeneratedRackGainPolicy::apply (nonAdditive);
    if (std::abs (nonAdditiveScale - 1.0f) > 1.0e-6f || std::abs (nonAdditive.layerGain[0] - 1.0f) > 1.0e-6f)
        return fail ("gain budget altered a non-additive layer operation");

    std::cout << "Phase A deterministic corpus, clean-path and generated-rack safety tests passed.\n";
    return 0;
}