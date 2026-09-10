#include <JuceHeader.h>
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

    std::cout << "Reference-relative spectral safety tests passed.\n";
    return 0;
}
