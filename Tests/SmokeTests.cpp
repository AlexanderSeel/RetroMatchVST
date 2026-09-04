#include <JuceHeader.h>
#include "../Source/Analysis/SampleAnalyzer.h"
#include "../Source/Matching/OfflineRenderer.h"
#include "../Source/Matching/SoundMatcher.h"
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

int main()
{
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
    return 0;
}
