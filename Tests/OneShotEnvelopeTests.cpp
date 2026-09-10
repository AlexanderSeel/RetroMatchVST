#include <JuceHeader.h>
#include "../Source/Analysis/SampleAnalyzer.h"
#include "../Source/Matching/OfflineRenderer.h"
#include "../Source/Matching/SoundMatcher.h"
#include <cmath>
#include <iostream>

namespace
{
int fail (const char* message)
{
    std::cerr << "FAILED: " << message << '\n';
    return 1;
}

float rmsBetween (const juce::AudioBuffer<float>& audio, int startSample, int endSample)
{
    startSample = juce::jlimit (0, audio.getNumSamples(), startSample);
    endSample = juce::jlimit (startSample, audio.getNumSamples(), endSample);
    if (endSample <= startSample) return 0.0f;

    double energy = 0.0;
    int values = 0;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        const auto* data = audio.getReadPointer (ch);
        for (int i = startSample; i < endSample; ++i)
        {
            energy += (double) data[i] * data[i];
            ++values;
        }
    }
    return values > 0 ? (float) std::sqrt (energy / values) : 0.0f;
}
}

int main()
{
    constexpr double sampleRate = 22050.0;
    constexpr float fundamental = 110.0f;
    constexpr float sourceDuration = 0.55f;
    const int sourceSamples = (int) std::round (sampleRate * sourceDuration);

    // A real one-shot fixture: a pitched hit whose natural decay reaches the
    // noise floor while the virtual MIDI key would still be held.
    juce::AudioBuffer<float> oneShotAudio (1, sourceSamples);
    for (int i = 0; i < sourceSamples; ++i)
    {
        const float t = i / (float) sampleRate;
        const float envelope = std::exp (-8.0f * t / sourceDuration);
        oneShotAudio.setSample (0, i, 0.72f * envelope
            * std::sin ((float) (juce::MathConstants<double>::twoPi * fundamental * t)));
    }

    const auto reference = SampleAnalyzer::analyzeBuffer (oneShotAudio, sampleRate, fundamental);
    const auto seedResult = SoundMatcher::initialFit (reference);
    const auto& seed = seedResult.params;

    if (seed.sustain > 1.0e-6f)
        return fail ("one-shot seed retained a non-zero keyboard sustain");
    for (const auto sustain : seed.fmOpSustain)
        if (sustain > 1.0e-6f)
            return fail ("one-shot FM operator retained a non-zero sustain");
    if (seed.mseg.enabled && seed.mseg.loopEnabled)
        return fail ("one-shot seed retained a looping MSEG");

    const auto heldSeed = OfflineRenderer::renderPatch (seed, sampleRate, 1.30f, fundamental, 128, {}, true);
    const int bodyEnd = (int) std::round (sampleRate * 0.18);
    const int tailStart = (int) std::round (sampleRate * 0.85);
    const float bodyRms = rmsBetween (heldSeed, 0, bodyEnd);
    const float tailRms = rmsBetween (heldSeed, tailStart, heldSeed.getNumSamples());
    if (bodyRms <= 1.0e-5f)
        return fail ("one-shot regression fixture rendered silence");
    if (tailRms > bodyRms * 0.03f + 1.0e-5f)
        return fail ("one-shot patch did not self-terminate while MIDI note remained held");

    MatchSettings settings;
    settings.renderSampleRate = sampleRate;
    settings.maxRenderSeconds = 1.30f;
    settings.iterations = 5;
    settings.topologyTrials = 2;
    settings.populationSize = 3;

    const auto goodScore = SoundMatcher::evaluateFit (reference, seed, settings);
    if (goodScore.tailSilenceSimilarity < 0.80f)
        return fail ("self-terminating candidate did not receive a strong held-tail score");

    auto sustainedCandidate = seed;
    sustainedCandidate.sustain = 0.72f;
    sustainedCandidate.mseg.enabled = false;
    sustainedCandidate.delayMix = sustainedCandidate.reverbMix = 0.0f;
    for (auto& sustain : sustainedCandidate.fmOpSustain) sustain = 0.72f;

    const auto badScore = SoundMatcher::evaluateFit (reference, sustainedCandidate, settings);
    if (badScore.tailSilenceSimilarity < 0.0f || badScore.tailSilenceSimilarity > 0.25f)
        return fail ("held-note scorer failed to expose a sustained post-source tail");
    if (badScore.tailSilenceSimilarity >= goodScore.tailSilenceSimilarity - 0.40f)
        return fail ("sustained and self-terminating candidates were not separated by tail scoring");

    const auto refined = SoundMatcher::refineFit (reference, sustainedCandidate, settings);
    if (refined.params.sustain > 1.0e-6f)
        return fail ("optimizer mutated a one-shot back into keyboard sustain");
    for (const auto sustain : refined.params.fmOpSustain)
        if (sustain > 1.0e-6f)
            return fail ("optimizer mutated an FM operator back into sustain");
    if (refined.params.mseg.enabled && refined.params.mseg.loopEnabled)
        return fail ("optimizer re-enabled a looping one-shot MSEG");
    if (refined.tailSilenceSimilarity < 0.70f)
        return fail ("refined one-shot retained excessive energy after the reference ended");

    // Guard against over-classification: a short held tone has a fast attack but
    // strong sustain and must remain playable as a normal keyboard-sustained patch.
    juce::AudioBuffer<float> sustainedAudio (1, (int) std::round (sampleRate * 0.80));
    for (int i = 0; i < sustainedAudio.getNumSamples(); ++i)
    {
        const float t = i / (float) sampleRate;
        const float fadeIn = juce::jlimit (0.0f, 1.0f, t / 0.008f);
        sustainedAudio.setSample (0, i, 0.55f * fadeIn
            * std::sin ((float) (juce::MathConstants<double>::twoPi * fundamental * t)));
    }
    const auto heldReference = SampleAnalyzer::analyzeBuffer (sustainedAudio, sampleRate, fundamental);
    const auto heldToneSeed = SoundMatcher::initialFit (heldReference).params;
    if (heldToneSeed.sustain < 0.10f)
        return fail ("short sustained tone was incorrectly classified as a self-terminating one-shot");

    std::cout << "RetroMatch one-shot envelope tests passed. tailGood="
              << goodScore.tailSilenceSimilarity << " tailBad=" << badScore.tailSilenceSimilarity
              << " refined=" << refined.tailSilenceSimilarity << '\n';
    return 0;
}
