#include <JuceHeader.h>
#include "../Source/Matching/ReferenceIdentity.h"
#include "../Source/Matching/RenderTelemetry.h"
#include "../Source/Matching/ResynthesisAdvisor.h"
#include "../Source/Matching/SimilarityScorer.h"

#include <cmath>
#include <iostream>

namespace
{
int fail (const char* message)
{
    std::cerr << "Similarity safety test failed: " << message << '\n';
    return 1;
}

SoundFeatures makeReference()
{
    SoundFeatures f;
    f.sampleRate = 48000.0;
    f.duration = 1.0f;
    f.rms = 0.18f;
    f.peak = 0.42f;
    f.fundamentalHz = 220.0f;
    f.pitchConfidence = 0.92f;
    f.spectralCentroidHz = 1700.0f;
    f.spectralRolloffHz = 3200.0f;
    f.spectralBandwidthHz = 2100.0f;
    f.spectralFlatness = 0.08f;
    f.oddHarmonicRatio = 0.48f;
    f.lowEnergyRatio = 0.24f;
    f.highEnergyRatio = 0.05f;
    f.zeroCrossingRate = 0.06f;
    f.harmonicity = 0.86f;
    f.inharmonicity = 0.08f;
    f.transientScore = 0.42f;
    f.attackSeconds = 0.008f;
    f.decaySeconds = 0.24f;
    f.sustainLevel = 0.62f;
    f.releaseSeconds = 0.18f;
    f.stereoWidth = 0.12f;
    f.spectralMotion = 0.04f;
    f.spectralBands.fill (0.20f);
    f.temporalRms.fill (0.55f);
    for (auto& frame : f.temporalSpectralBands) frame.fill (0.20f);
    f.timbreCepstrum.fill (0.10f);
    return f;
}
}

int main()
{
    const auto reference = makeReference();

    const auto identical = SimilarityScorer::compare (reference, reference);
    if (std::abs (identical.spectralSafety - 1.0f) > 1.0e-6f)
        return fail ("an identical candidate was penalized");

    juce::AudioBuffer<float> collapsedRender (1, 64);
    collapsedRender.clear();
    collapsedRender.getWritePointer (0)[0] = 0.25f;
    for (int sample = 1; sample < collapsedRender.getNumSamples(); ++sample)
        collapsedRender.getWritePointer (0)[sample] = 0.25f;
    const auto collapsedTelemetry = RenderTelemetry::analyze (collapsedRender);
    if (collapsedTelemetry.technicalSafetyScore() >= 0.99f
        || std::abs (identical.total - 1.0f) > 1.0e-6f)
        return fail ("technical collapse was not kept separate from perceptual similarity");

    auto moderate = reference;
    moderate.highEnergyRatio = 0.13f;
    moderate.spectralRolloffHz = 4400.0f;
    moderate.spectralCentroidHz = 2100.0f;
    const auto moderateScore = SimilarityScorer::compare (reference, moderate);
    if (std::abs (moderateScore.spectralSafety - 1.0f) > 1.0e-6f)
        return fail ("ordinary brightness movement triggered the harshness guard");

    auto unsupported = reference;
    unsupported.highEnergyRatio = 0.70f;
    unsupported.spectralRolloffHz = 18000.0f;
    unsupported.spectralCentroidHz = 9200.0f;
    unsupported.spectralBandwidthHz = 10500.0f;
    const auto unsupportedScore = SimilarityScorer::compare (reference, unsupported);
    if (unsupportedScore.spectralSafety > 0.70f
        || unsupportedScore.total >= moderateScore.total)
        return fail ("unsupported upper-band expansion was not materially penalized");

    auto brightReference = reference;
    brightReference.highEnergyRatio = 0.55f;
    brightReference.spectralRolloffHz = 16000.0f;
    brightReference.spectralCentroidHz = 7600.0f;
    brightReference.spectralBandwidthHz = 9000.0f;
    auto brightCandidate = brightReference;
    brightCandidate.highEnergyRatio = 0.65f;
    brightCandidate.spectralRolloffHz = 19000.0f;
    brightCandidate.spectralCentroidHz = 8400.0f;
    const auto brightScore = SimilarityScorer::compare (brightReference, brightCandidate);
    if (brightScore.spectralSafety < 0.99f)
        return fail ("a legitimately bright reference did not receive evidence-based allowance");

    auto noisyHit = reference;
    noisyHit.duration = 0.28f;
    noisyHit.pitchConfidence = 0.18f;
    noisyHit.harmonicity = 0.18f;
    noisyHit.inharmonicity = 0.72f;
    noisyHit.spectralFlatness = 0.72f;
    noisyHit.transientScore = 0.90f;
    noisyHit.sustainLevel = 0.08f;
    noisyHit.temporalRms = {{ 1.0f, 0.72f, 0.35f, 0.16f, 0.07f, 0.03f, 0.01f, 0.0f }};
    const auto noisyIdentity = ReferenceIdentity::classify (noisyHit);
    if (noisyIdentity.pitchModel != ReferenceIdentity::PitchModel::unpitched
        || noisyIdentity.lifecycle != ReferenceIdentity::Lifecycle::oneShot)
        return fail ("noisy percussion was forced into a pitched or sustained identity");
    const auto noisyAdvice = ResynthesisAdvisor::advise (noisyHit);
    if (noisyAdvice.sourceFamily != "percussive / transient" || noisyAdvice.method == 1 || noisyAdvice.method == 2)
        return fail ("noisy percussion advice still prefers a harmonic oscillator topology");

    auto mallet = reference;
    mallet.duration = 1.10f;
    mallet.pitchConfidence = 0.82f;
    mallet.harmonicity = 0.48f;
    mallet.inharmonicity = 0.42f;
    mallet.spectralFlatness = 0.14f;
    mallet.transientScore = 0.68f;
    mallet.sustainLevel = 0.20f;
    mallet.temporalRms = {{ 1.0f, 0.82f, 0.57f, 0.36f, 0.20f, 0.10f, 0.04f, 0.01f }};
    const auto malletIdentity = ReferenceIdentity::classify (mallet);
    if (malletIdentity.pitchModel != ReferenceIdentity::PitchModel::inharmonicPitched
        || malletIdentity.lifecycle != ReferenceIdentity::Lifecycle::plucked)
        return fail ("mallet identity did not preserve pitched-but-inharmonic evidence");
    const auto malletAdvice = ResynthesisAdvisor::advise (mallet);
    if (malletAdvice.sourceFamily != "mallet / metallic pitched" || malletAdvice.method == 2)
        return fail ("mallet advice collapsed back to a subtractive-only explanation");

    auto evolving = reference;
    evolving.duration = 4.0f;
    evolving.sustainLevel = 0.76f;
    evolving.spectralMotion = 0.24f;
    evolving.temporalRms.fill (0.62f);
    const auto evolvingIdentity = ReferenceIdentity::classify (evolving);
    if (evolvingIdentity.lifecycle != ReferenceIdentity::Lifecycle::evolving
        || evolvingIdentity.selfTerminates())
        return fail ("evolving sustained material was misclassified as self-terminating");

    std::cout << "Reference-relative spectral safety and explicit identity tests passed.\n";
    return 0;
}
