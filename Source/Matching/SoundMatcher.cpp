#include "SoundMatcher.h"
#include "OfflineRenderer.h"
#include "EffectChainProbe.h"
#include "GeneratedRackGainPolicy.h"

namespace
{
thread_local RenderTelemetry lastCoreRenderTelemetry {};
thread_local bool lastCoreRenderTelemetryValid = false;

struct TelemetryOfflineRenderer
{
    static juce::AudioBuffer<float> renderPatch (const VoiceParameters& params,
                                                  double sampleRate,
                                                  float durationSeconds,
                                                  float targetFundamentalHz,
                                                  int blockSize = 256,
                                                  DspRouting::Plan routingPlan = {},
                                                  bool holdNote = false)
    {
        auto audio = OfflineRenderer::renderPatch (params, sampleRate, durationSeconds,
                                                   targetFundamentalHz, blockSize,
                                                   std::move (routingPlan), holdNote);
        lastCoreRenderTelemetry = RenderTelemetry::analyze (audio);
        lastCoreRenderTelemetryValid = true;
        return audio;
    }
};

struct TelemetrySampleAnalyzer
{
    static SoundFeatures analyzeBuffer (const juce::AudioBuffer<float>& audio,
                                        double sampleRate,
                                        float expectedFundamentalHz = 0.0f)
    {
        // Do not feed NaN/Inf into spectral analysis. The paired safety scorer
        // will hard-reject this candidate by assigning a zero technical score.
        if (lastCoreRenderTelemetryValid && ! lastCoreRenderTelemetry.isFinite())
        {
            SoundFeatures invalid;
            invalid.sampleRate = sampleRate;
            invalid.duration = static_cast<float> (audio.getNumSamples() / juce::jmax (1.0, sampleRate));
            invalid.fundamentalHz = expectedFundamentalHz;
            return invalid;
        }
        return SampleAnalyzer::analyzeBuffer (audio, sampleRate, expectedFundamentalHz);
    }
};

struct TelemetrySimilarityScorer
{
    static SimilarityBreakdown compare (const SoundFeatures& reference, const SoundFeatures& candidate)
    {
        auto result = SimilarityScorer::compare (reference, candidate);
        if (lastCoreRenderTelemetryValid)
            result.total = juce::jlimit (0.0f, 1.0f,
                result.total * lastCoreRenderTelemetry.technicalSafetyScore (reference.rms));
        return result;
    }
};

struct TelemetryEffectChainProbe
{
    static float score (const SoundFeatures& reference, const VoiceParameters& candidate)
    {
        const float raw = EffectChainProbe::score (reference, candidate);
        if (! lastCoreRenderTelemetryValid)
            return raw;
        return juce::jlimit (0.0f, 1.0f, raw * lastCoreRenderTelemetry.technicalSafetyScore (reference.rms));
    }
};

void attachTechnicalTelemetry (MatchResult& result, const RenderTelemetry& telemetry, float referenceRms)
{
    result.renderTelemetry = telemetry;
    result.technicalSafetyScore = telemetry.technicalSafetyScore (referenceRms);
    result.technicallySafe = telemetry.isTechnicallySafe();
}
}

// Keep the established optimizer implementation intact as a core and wrap its
// public entry points with reference-lifecycle and technical-safety policy.
// Instrumenting the existing render/analyze/score calls avoids a second render
// for every population candidate while still making safety part of ranking.
#define OfflineRenderer TelemetryOfflineRenderer
#define SampleAnalyzer TelemetrySampleAnalyzer
#define SimilarityScorer TelemetrySimilarityScorer
#define EffectChainProbe TelemetryEffectChainProbe
#define initialFit initialFitCore
#define evaluateFit evaluateFitCore
#define refineFit refineFitCore
#include "SoundMatcherCore.inc"
#undef refineFit
#undef evaluateFit
#undef initialFit
#undef EffectChainProbe
#undef SimilarityScorer
#undef SampleAnalyzer
#undef OfflineRenderer

namespace
{
bool isSelfTerminatingReference (const SoundFeatures& f)
{
    if (f.duration <= 0.0f) return false;

    float earlyEnergy = 0.0f;
    for (int i = 0; i < SoundFeatures::temporalFrameCount / 2; ++i)
        earlyEnergy = juce::jmax (earlyEnergy, f.temporalRms[(size_t) i]);

    const float lateEnergy = 0.5f * (f.temporalRms[(size_t) SoundFeatures::temporalFrameCount - 2]
                                   + f.temporalRms[(size_t) SoundFeatures::temporalFrameCount - 1]);
    const bool temporalDecay = earlyEnergy > 1.0e-4f
                            && lateEnergy < juce::jmax (0.08f, earlyEnergy * 0.28f);
    const bool lowSustain = f.sustainLevel < 0.48f;
    const bool moderatelyLowSustain = f.sustainLevel < 0.62f;

    const bool shortDecayingHit = f.duration < 1.35f
                               && moderatelyLowSustain
                               && (temporalDecay || f.transientScore > 0.55f);
    const bool strongTransientDecay = f.transientScore > 0.70f
                                    && lowSustain;
    const bool naturallyDecaying = temporalDecay
                                && moderatelyLowSustain
                                && f.duration < 4.0f;
    return shortDecayingHit || strongTransientDecay || naturallyDecaying;
}

float activeReferenceDuration (const SoundFeatures& f)
{
    const float trailingSilence = juce::jlimit (0.0f, f.duration * 0.65f, f.releaseSeconds);
    return juce::jmax (0.025f, f.duration - trailingSilence);
}

float referenceDecayDuration (const SoundFeatures& f)
{
    const float active = activeReferenceDuration (f);
    return juce::jlimit (0.02f, 5.0f, active - juce::jmin (f.attackSeconds, active * 0.35f));
}

void removeTimeBasedTailEffects (VoiceParameters& p)
{
    p.delayMix = 0.0f;
    p.delayFeedback = 0.0f;
    p.reverbMix = 0.0f;

    auto removeDelayAndReverb = [] (auto& modules)
    {
        for (auto& module : modules)
            if (module.type == 8 || module.type == 9)
                module = {};
    };
    removeDelayAndReverb (p.fxModules);
    removeDelayAndReverb (p.globalFxModules);
}

void enforceSelfTerminatingEnvelope (VoiceParameters& p, const SoundFeatures& reference)
{
    if (! isSelfTerminatingReference (reference)) return;

    const float active = activeReferenceDuration (reference);
    const float targetDecay = referenceDecayDuration (reference);
    const float minDecay = juce::jmax (0.02f, targetDecay * 0.72f);
    const float maxDecay = juce::jmax (minDecay, juce::jmin (5.0f, targetDecay * 1.18f));

    p.attack = juce::jlimit (0.001f, juce::jmax (0.002f, juce::jmin (0.080f, active * 0.30f)), p.attack);
    p.decay = juce::jlimit (minDecay, maxDecay, p.decay);
    p.sustain = 0.0f;
    p.release = juce::jlimit (0.005f, 0.18f,
                              juce::jmin (p.release, juce::jmax (0.025f, reference.releaseSeconds)));

    p.wavetableMix = juce::jlimit (0.0f, 1.0f, p.wavetableMix);
    p.supersawMix = juce::jlimit (0.0f, 1.0f, p.supersawMix);
    p.wavefold = juce::jlimit (0.0f, 1.0f, p.wavefold);

    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        p.fmOpFixedMode[(size_t) i] = juce::jlimit (0, 1, p.fmOpFixedMode[(size_t) i]);
        p.fmOpAttack[(size_t) i] = juce::jlimit (0.001f, 5.0f, p.fmOpAttack[(size_t) i]);
        p.fmOpSustain[(size_t) i] = 0.0f;
        const float operatorMax = juce::jlimit (0.015f, 5.0f, targetDecay * (0.72f + i * 0.08f));
        p.fmOpDecay[(size_t) i] = juce::jlimit (0.005f, operatorMax, p.fmOpDecay[(size_t) i]);
        p.fmOpRelease[(size_t) i] = juce::jlimit (0.005f, 0.20f, p.fmOpRelease[(size_t) i]);
    }

    if (p.mseg.enabled)
    {
        p.mseg.loopEnabled = false;
        if (p.msegTarget == (int) ModDestination::amplitude)
        {
            p.mseg.levels[0] = 0.0f;
            p.mseg.levels[1] = 1.0f;
            p.mseg.levels[4] = juce::jmin (p.mseg.levels[4], 0.08f);
            p.mseg.levels[5] = 0.0f;

            const float attackTime = juce::jlimit (0.001f, juce::jmax (0.002f, active * 0.25f), p.attack);
            const float remaining = juce::jmax (0.020f, active - attackTime);
            p.mseg.times = {{ attackTime,
                              remaining * 0.18f,
                              remaining * 0.24f,
                              remaining * 0.28f,
                              remaining * 0.30f }};
        }
    }

    removeTimeBasedTailEffects (p);
}

void enforceSelfTerminatingTree (VoiceParameters& p, const SoundFeatures& reference, int depth = 0)
{
    enforceSelfTerminatingEnvelope (p, reference);
    if (depth >= VoiceParameters::extraLayerCount) return;

    for (auto& layer : p.layers)
    {
        if (! layer) continue;
        auto mutableLayer = std::make_shared<VoiceParameters> (*layer);
        enforceSelfTerminatingTree (*mutableLayer, reference, depth + 1);
        layer = std::move (mutableLayer);
    }
}

float rmsBetween (const juce::AudioBuffer<float>& audio, int startSample, int endSample)
{
    startSample = juce::jlimit (0, audio.getNumSamples(), startSample);
    endSample = juce::jlimit (startSample, audio.getNumSamples(), endSample);
    if (endSample <= startSample || audio.getNumChannels() <= 0) return 0.0f;

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

bool SoundMatcher::referenceSelfTerminates (const SoundFeatures& reference)
{
    return isSelfTerminatingReference (reference);
}

void SoundMatcher::enforceReferenceLifecycle (const SoundFeatures& reference, VoiceParameters& params)
{
    // Layered generated racks pass through this invariant even for sustained references.
    // Single-voice candidates are explicitly ignored by the gain policy.
    GeneratedRackGainPolicy::apply (params);
    if (! isSelfTerminatingReference (reference)) return;
    enforceSelfTerminatingTree (params, reference);
}

MatchResult SoundMatcher::initialFit (const SoundFeatures& reference)
{
    auto result = initialFitCore (reference);
    if (isSelfTerminatingReference (reference))
    {
        enforceReferenceLifecycle (reference, result.params);
        result.explanation += " Self-terminating reference: sustain is zero, decay follows the audible source duration, FM operator sustains are zero and amplitude motion cannot loop.";
    }
    return result;
}

MatchResult SoundMatcher::evaluateFit (const SoundFeatures& reference,
                                        const VoiceParameters& params,
                                        const MatchSettings& settings)
{
    const auto safeSettings = settings.bounded();
    if (! isSelfTerminatingReference (reference))
    {
        lastCoreRenderTelemetryValid = false;
        auto result = evaluateFitCore (reference, params, safeSettings);
        if (lastCoreRenderTelemetryValid)
            attachTechnicalTelemetry (result, lastCoreRenderTelemetry, reference.rms);
        return result;
    }

    MatchResult result;
    result.params = params;

    const float maxRender = juce::jmax (0.45f, safeSettings.maxRenderSeconds);
    const float comparisonDuration = juce::jlimit (0.12f, maxRender,
                                                   juce::jmax (0.12f, reference.duration));
    const float tailProbeSeconds = juce::jlimit (0.30f, 0.90f,
                                                 juce::jmax (0.30f, reference.duration * 0.55f));
    const float renderDuration = juce::jmin (maxRender, comparisonDuration + tailProbeSeconds);

    auto audio = OfflineRenderer::renderPatch (params, safeSettings.renderSampleRate, renderDuration,
                                               reference.fundamentalHz, 256, {}, true);
    const auto telemetry = RenderTelemetry::analyze (audio);
    attachTechnicalTelemetry (result, telemetry, reference.rms);

    if (! telemetry.isFinite() || telemetry.finiteSamples <= 0)
    {
        result.similarity.total = 0.0f;
        result.confidence = 0.0f;
        result.evaluatedCandidates = 1;
        result.explanation = "Candidate rejected: offline render contained non-finite samples or no finite audio.";
        return result;
    }

    const int comparisonSamples = juce::jlimit (1, audio.getNumSamples(),
        (int) std::round (comparisonDuration * safeSettings.renderSampleRate));
    juce::AudioBuffer<float> comparisonAudio (audio.getNumChannels(), comparisonSamples);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        comparisonAudio.copyFrom (ch, 0, audio, ch, 0, comparisonSamples);

    result.candidateFeatures = SampleAnalyzer::analyzeBuffer (comparisonAudio, safeSettings.renderSampleRate,
                                                               reference.fundamentalHz);
    result.similarity = SimilarityScorer::compare (reference, result.candidateFeatures);

    if (safeSettings.algorithm == 6)
    {
        result.effectProbeSimilarity = EffectChainProbe::score (reference, params);
        result.similarity.total = juce::jlimit (0.0f, 1.0f,
            result.similarity.total * 0.95f + result.effectProbeSimilarity * 0.05f);
    }

    const int graceSamples = (int) std::round (0.035 * safeSettings.renderSampleRate);
    const int tailStart = juce::jmin (audio.getNumSamples(), comparisonSamples + graceSamples);
    if (tailStart < audio.getNumSamples())
    {
        const float bodyRms = juce::jmax (1.0e-5f, rmsBetween (comparisonAudio, 0, comparisonAudio.getNumSamples()));
        const float tailRms = rmsBetween (audio, tailStart, audio.getNumSamples());
        const float tailRatio = tailRms / bodyRms;
        result.tailSilenceSimilarity = 1.0f - juce::jlimit (0.0f, 1.0f, (tailRatio - 0.01f) / 0.12f);
        result.similarity.total = juce::jlimit (0.0f, 1.0f,
            result.similarity.total * (0.70f + 0.30f * result.tailSilenceSimilarity));
    }

    result.similarity.total = juce::jlimit (0.0f, 1.0f,
        result.similarity.total * telemetry.technicalSafetyScore (reference.rms));
    result.confidence = result.similarity.total;
    result.evaluatedCandidates = 1;
    return result;
}

MatchResult SoundMatcher::refineFit (const SoundFeatures& reference,
                                     const VoiceParameters& seed,
                                     const MatchSettings& settings,
                                     ProgressCallback progress,
                                     CancelCallback cancel)
{
    const auto safeSettings = settings.bounded();
    if (! isSelfTerminatingReference (reference))
    {
        lastCoreRenderTelemetryValid = false;
        auto best = refineFitCore (reference, seed, safeSettings, std::move (progress), std::move (cancel));

        // The last population render is not necessarily the winning candidate.
        // Re-evaluate only the winner so the exposed telemetry belongs to exactly
        // the patch/score returned to callers while selection itself remained
        // safety-aware during the core optimization loop.
        const int evaluatedCandidates = best.evaluatedCandidates;
        const auto explanation = best.explanation;
        const int algorithm = best.algorithm;
        const int complexity = best.complexity;
        const bool fullRackScore = best.fullRackScore;
        auto verified = evaluateFit (reference, best.params, safeSettings);
        verified.evaluatedCandidates = evaluatedCandidates;
        verified.explanation = explanation;
        verified.algorithm = algorithm;
        verified.complexity = complexity;
        verified.fullRackScore = fullRackScore;
        return verified;
    }

    juce::Random random ((int64) 0x524d5333);
    std::vector<MatchResult> elite;
    elite.reserve ((size_t) juce::jmax (2, safeSettings.populationSize));

    auto insertElite = [&] (MatchResult candidate)
    {
        elite.push_back (std::move (candidate));
        std::sort (elite.begin(), elite.end(), [] (const auto& a, const auto& b)
        {
            return a.similarity.total > b.similarity.total;
        });
        if ((int) elite.size() > juce::jmax (2, safeSettings.populationSize))
            elite.resize ((size_t) safeSettings.populationSize);
    };

    auto profiledSeed = seed;
    applyAlgorithmProfile (profiledSeed, safeSettings.algorithm, reference);
    enforceReferenceLifecycle (reference, profiledSeed);
    insertElite (evaluateFit (reference, profiledSeed, settings));
    int evaluated = 1;
    const int total = juce::jmax (1, 1 + safeSettings.topologyTrials + safeSettings.iterations);
    auto report = [&] { if (progress) progress (juce::jlimit (0.0f, 1.0f, evaluated / (float) total)); };
    report();

    for (int i = 0; i < safeSettings.topologyTrials; ++i)
    {
        if (cancel && cancel()) break;
        auto candidate = profiledSeed;
        if (i < 5)
        {
            isolateOscillator (candidate, i == 4 ? 0 : i);
            if (i == 4 && candidate.referenceWavetable && candidate.referenceWavetable->valid)
            {
                candidate.osc1Mix = 0.0f;
                candidate.referenceWavetableMix = 0.85f;
                candidate.cutoff = 19000.0f;
            }
        }
        else
        {
            candidate.osc1Wave = random.nextInt (5);
            candidate.osc2Wave = random.nextInt (5);
            candidate.filterType = random.nextInt (3);
            candidate.fmAlgorithm = random.nextInt (6);
            if (random.nextBool()) candidate.fmAmount = random.nextFloat() * 0.30f;
            if (random.nextBool()) candidate.fmMix = random.nextFloat() * 0.55f;
            if (random.nextBool()) candidate.ringMix = random.nextFloat() * 0.25f;
            if (random.nextBool()) candidate.wavetableMix = random.nextFloat() * 0.55f;
            if (random.nextBool()) candidate.supersawMix = random.nextFloat() * 0.48f;
            if (profiledSeed.wavefold > 0.02f && random.nextFloat() < 0.20f)
                candidate.wavefold = random.nextFloat() * juce::jmin (0.42f, juce::jmax (0.08f, profiledSeed.wavefold * 1.6f));
            if (random.nextFloat() < 0.25f)
                candidate.fmOpFixedMode[(size_t) random.nextInt (VoiceParameters::fmOperatorCount)] = 1;
            candidate = mutate (candidate, random, 0.13f, false);
        }
        applyAlgorithmProfile (candidate, safeSettings.algorithm, reference);
        applyLocks (candidate, seed, settings);
        enforceReferenceLifecycle (reference, candidate);
        insertElite (evaluateFit (reference, candidate, settings));
        ++evaluated;
        report();
    }

    int stagnant = 0;
    float lastBest = elite.front().similarity.total;
    for (int i = 0; i < safeSettings.iterations; ++i)
    {
        if (cancel && cancel()) break;
        const float phase = i / (float) juce::jmax (1, safeSettings.iterations - 1);
        float amount = juce::jmap (phase, 0.0f, 1.0f, 0.16f, 0.014f);
        if (stagnant > 18) amount = juce::jmax (amount, 0.075f);
        const bool topology = i < safeSettings.iterations / 3 || stagnant > 24;

        const int parentIndex = juce::jmin ((int) elite.size() - 1,
                                            (int) std::floor (std::pow (random.nextFloat(), 2.2f) * elite.size()));
        auto candidate = mutate (elite[(size_t) parentIndex].params, random, amount, topology);
        if (i % 4 == 0)
        {
            const auto mutated = candidate;
            candidate = elite[(size_t) parentIndex].params;
            candidate.attack = mutated.attack;
            candidate.decay = mutated.decay;
            candidate.sustain = mutated.sustain;
            candidate.release = mutated.release;
            candidate.cutoff = mutated.cutoff;
            candidate.resonance = mutated.resonance;
        }
        else if (i % 4 == 1)
        {
            const auto mutated = candidate;
            candidate = elite[(size_t) parentIndex].params;
            candidate.lfoRate = mutated.lfoRate;
            candidate.lfoPitch = mutated.lfoPitch;
            candidate.lfoCutoff = mutated.lfoCutoff;
            candidate.lfoAmp = mutated.lfoAmp;
            candidate.modSlots = mutated.modSlots;
            candidate.modGraphSlots = mutated.modGraphSlots;
            candidate.moduleModSlots = mutated.moduleModSlots;
            candidate.extraLfoRate = mutated.extraLfoRate;
            candidate.extraLfoShape = mutated.extraLfoShape;
            candidate.mseg = mutated.mseg;
            candidate.fxModules = mutated.fxModules;
        }
        applyAlgorithmProfile (candidate, safeSettings.algorithm, reference);
        applyLocks (candidate, seed, settings);

        if (elite.size() > 1 && random.nextFloat() < 0.22f)
        {
            const auto& donor = elite[(size_t) random.nextInt ((int) elite.size())].params;
            if (random.nextBool()) { candidate.cutoff = donor.cutoff; candidate.resonance = donor.resonance; }
            if (random.nextBool()) { candidate.attack = donor.attack; candidate.decay = donor.decay; candidate.sustain = donor.sustain; candidate.release = donor.release; }
            if (random.nextBool()) { candidate.fmAlgorithm = donor.fmAlgorithm; candidate.fmMix = donor.fmMix; candidate.fmFeedback = donor.fmFeedback; }
            if (random.nextBool()) { candidate.wavetableMix = donor.wavetableMix; candidate.wavetablePosition = donor.wavetablePosition; candidate.supersawMix = donor.supersawMix; candidate.wavefold = donor.wavefold; }
            if (random.nextFloat() < 0.35f) { candidate.fmOpAttack = donor.fmOpAttack; candidate.fmOpDecay = donor.fmOpDecay; candidate.fmOpSustain = donor.fmOpSustain; candidate.fmOpRelease = donor.fmOpRelease; }
            if (random.nextBool()) { candidate.chorusMix = donor.chorusMix; candidate.reverbMix = donor.reverbMix; candidate.stereoWidth = donor.stereoWidth; }
        }

        applyAlgorithmProfile (candidate, safeSettings.algorithm, reference);
        applyLocks (candidate, seed, settings);
        enforceReferenceLifecycle (reference, candidate);
        insertElite (evaluateFit (reference, candidate, settings));
        ++evaluated;
        const float nowBest = elite.front().similarity.total;
        if (nowBest > lastBest + 0.00025f) { lastBest = nowBest; stagnant = 0; }
        else ++stagnant;
        report();
    }

    auto best = elite.front();
    best.evaluatedCandidates = evaluated;
    best.confidence = best.similarity.total;
    best.explanation = "Population closed-loop one-shot match: candidate patches are constrained to self-terminate, rendered with the note held beyond the reference end, compared over the source-length window, and penalized for residual post-source energy.";
    if (safeSettings.algorithm == 6)
        best.explanation += " FX / Guitar Chain keeps its diagnostic transfer score, while delay/reverb tails are removed for self-terminating references.";
    if (progress) progress (1.0f);
    return best;
}
