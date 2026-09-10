#include <JuceHeader.h>
#include "../Source/Matching/CompareFineTune.h"
#include "../Source/Matching/OfflineRenderer.h"
#include "../Source/Analysis/SampleAnalyzer.h"
#include <cmath>
#include <iostream>

namespace
{
int fail (const char* message)
{
    std::cerr << "FAILED: " << message << '\n';
    return 1;
}

bool finiteAudio (const juce::AudioBuffer<float>& audio)
{
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (! std::isfinite (audio.getSample (ch, i))) return false;
    return true;
}

bool near (float a, float b, float epsilon = 1.0e-6f)
{
    return std::abs (a - b) <= epsilon;
}
}

int main()
{
    VoiceParameters baseline;
    baseline.osc1Wave = 1;
    baseline.osc1Mix = 0.9f;
    baseline.osc2Mix = 0.0f;
    baseline.subMix = 0.18f;
    baseline.cutoff = 1500.0f;
    baseline.attack = 0.08f;
    baseline.decay = 0.24f;
    baseline.sustain = 0.62f;
    baseline.release = 0.35f;
    baseline.stereoWidth = 1.0f;
    baseline.supersawMix = 0.45f;
    baseline.unisonSpread = 0.55f;
    baseline.lfoCutoff = 0.20f;
    baseline.msegDepth = 0.30f;
    baseline.modGraphSlots[0] = { (int) ModSource::lfo1, (int) ModDestination::cutoff, 0.25f };

    auto layer = std::make_shared<VoiceParameters> (baseline);
    layer->layers.fill (nullptr);
    baseline.layers[0] = layer;
    baseline.layerPan[0] = 0.4f;

    const auto neutral = CompareFineTune::apply (baseline, {});
    if (! near (neutral.cutoff, baseline.cutoff)
        || ! near (neutral.attack, baseline.attack)
        || ! near (neutral.stereoWidth, baseline.stereoWidth)
        || ! neutral.layers[0]
        || ! near (neutral.layers[0]->cutoff, baseline.layers[0]->cutoff))
        return fail ("neutral compare fine-tune changed the baseline");

    CompareFineTune::Values positive;
    positive.brightness = 0.75f;
    positive.lowEnd = 0.50f;
    positive.punch = 0.70f;
    positive.tail = 0.60f;
    positive.width = 0.70f;
    positive.motion = 0.65f;
    positive.finePitch = 0.40f;
    auto sameGeneration = positive;
    if (! positive.nearlyEquals (sameGeneration)) return fail ("fine-tune generation equality rejected identical values");
    sameGeneration.width += 0.02f;
    if (positive.nearlyEquals (sameGeneration)) return fail ("fine-tune generation equality missed a changed knob");
    const auto adjusted = CompareFineTune::apply (baseline, positive);

    if (adjusted.cutoff <= baseline.cutoff) return fail ("brightness did not raise cutoff");
    if (adjusted.subMix <= baseline.subMix) return fail ("low-end did not raise main-voice sub balance");
    if (adjusted.attack >= baseline.attack) return fail ("punch did not shorten attack");
    if (adjusted.release <= baseline.release || adjusted.decay <= baseline.decay)
        return fail ("tail did not lengthen decay/release");
    if (! near (adjusted.sustain, baseline.sustain))
        return fail ("tail changed sustain instead of musical tail time");
    if (adjusted.stereoWidth <= baseline.stereoWidth || adjusted.layerPan[0] <= baseline.layerPan[0])
        return fail ("width did not widen voice/layer image");
    if (! near (adjusted.unisonSpread, baseline.unisonSpread))
        return fail ("width changed unison detune/spread timbre");
    if (adjusted.modGraphSlots[0].amount <= baseline.modGraphSlots[0].amount
        || adjusted.msegDepth <= baseline.msegDepth)
        return fail ("motion did not scale existing modulation");
    if (std::abs (adjusted.masterTuneCents - 20.0f) > 0.01f)
        return fail ("fine-pitch mapping is incorrect");
    if (! adjusted.layers[0] || adjusted.layers[0].get() == baseline.layers[0].get())
        return fail ("full-rack fine-tune did not clone immutable layer state");
    if (! near (adjusted.layers[0]->subMix, baseline.layers[0]->subMix))
        return fail ("low-end multiplied sub oscillators across companion layers");
    if (! near (adjusted.layers[0]->sustain, baseline.layers[0]->sustain))
        return fail ("tail reintroduced sustain in a companion layer");
    if (! near (adjusted.layers[0]->unisonSpread, baseline.layers[0]->unisonSpread))
        return fail ("width changed companion unison timbre");
    if (std::abs (adjusted.layers[0]->masterTuneCents - 20.0f) > 0.01f)
        return fail ("fine pitch did not keep the full rack in tune");

    // Compare corrections must never alter nonlinear colour. A user can edit drive/fold/FX
    // elsewhere, but BRIGHTNESS/LOW END/PUNCH/TAIL/WIDTH/MOTION/PITCH must not secretly do it.
    auto colouredBaseline = baseline;
    colouredBaseline.drive = 0.21f;
    colouredBaseline.wavefold = 0.17f;
    colouredBaseline.distortionMode = 2;
    colouredBaseline.distortionMix = 0.37f;
    colouredBaseline.fxModules[0].type = 3;
    colouredBaseline.fxModules[0].amount = 0.26f;
    colouredBaseline.fxModules[0].mix = 0.31f;
    auto colouredLayer = std::make_shared<VoiceParameters> (*colouredBaseline.layers[0]);
    colouredLayer->drive = 0.13f;
    colouredLayer->wavefold = 0.09f;
    colouredBaseline.layers[0] = colouredLayer;
    const auto colouredAdjusted = CompareFineTune::apply (colouredBaseline, positive);
    if (! near (colouredAdjusted.drive, colouredBaseline.drive)
        || ! near (colouredAdjusted.wavefold, colouredBaseline.wavefold)
        || colouredAdjusted.distortionMode != colouredBaseline.distortionMode
        || ! near (colouredAdjusted.distortionMix, colouredBaseline.distortionMix)
        || colouredAdjusted.fxModules[0].type != colouredBaseline.fxModules[0].type
        || ! near (colouredAdjusted.fxModules[0].amount, colouredBaseline.fxModules[0].amount)
        || ! near (colouredAdjusted.fxModules[0].mix, colouredBaseline.fxModules[0].mix)
        || ! colouredAdjusted.layers[0]
        || ! near (colouredAdjusted.layers[0]->drive, colouredBaseline.layers[0]->drive)
        || ! near (colouredAdjusted.layers[0]->wavefold, colouredBaseline.layers[0]->wavefold))
        return fail ("Compare macro altered nonlinear colour/FX state");

    // A one-shot baseline stays a one-shot even with maximum positive TAIL.
    VoiceParameters oneShot = baseline;
    oneShot.sustain = 0.0f;
    for (auto& sustain : oneShot.fmOpSustain) sustain = 0.0f;
    auto oneShotLayer = std::make_shared<VoiceParameters> (*baseline.layers[0]);
    oneShotLayer->sustain = 0.0f;
    for (auto& sustain : oneShotLayer->fmOpSustain) sustain = 0.0f;
    oneShot.layers[0] = oneShotLayer;
    CompareFineTune::Values maxTail;
    maxTail.tail = 1.0f;
    const auto tailedOneShot = CompareFineTune::apply (oneShot, maxTail);
    if (! near (tailedOneShot.sustain, 0.0f) || ! tailedOneShot.layers[0]
        || ! near (tailedOneShot.layers[0]->sustain, 0.0f))
        return fail ("TAIL turned a self-terminating patch into a sustained patch");
    for (const auto sustain : tailedOneShot.fmOpSustain)
        if (! near (sustain, 0.0f)) return fail ("TAIL reintroduced FM sustain");

    CompareFineTune::Values staticMotion;
    staticMotion.motion = 1.0f;
    VoiceParameters staticPatch;
    staticPatch.mseg.enabled = false;
    staticPatch.msegDepth = 0.0f;
    const auto stillStatic = CompareFineTune::apply (staticPatch, staticMotion);
    if (stillStatic.mseg.enabled || std::abs (stillStatic.lfoPitch) > 1.0e-6f
        || std::abs (stillStatic.lfoCutoff) > 1.0e-6f || std::abs (stillStatic.lfoAmp) > 1.0e-6f)
        return fail ("motion macro invented modulation topology");

    SoundFeatures residualReference, residualCandidate;
    residualReference.spectralCentroidHz = 4200.0f; residualCandidate.spectralCentroidHz = 2500.0f;
    residualReference.lowEnergyRatio = 0.30f; residualCandidate.lowEnergyRatio = 0.16f;
    residualReference.attackSeconds = 0.02f; residualCandidate.attackSeconds = 0.08f;
    residualReference.decaySeconds = 0.50f; residualCandidate.decaySeconds = 0.24f;
    residualReference.releaseSeconds = 0.70f; residualCandidate.releaseSeconds = 0.30f;
    residualReference.sustainLevel = 0.75f; residualCandidate.sustainLevel = 0.55f;
    residualReference.stereoWidth = 0.65f; residualCandidate.stereoWidth = 0.22f;
    residualReference.spectralMotion = 0.40f; residualCandidate.spectralMotion = 0.12f;
    residualReference.fundamentalHz = 222.0f; residualCandidate.fundamentalHz = 220.0f;
    residualReference.pitchConfidence = residualCandidate.pitchConfidence = 0.9f;
    const auto nudge = CompareFineTune::suggestFromResidual (residualReference, residualCandidate, baseline);
    if (nudge.brightness <= 0.0f || nudge.lowEnd <= 0.0f || nudge.punch <= 0.0f
        || nudge.tail <= 0.0f || nudge.width <= 0.0f || nudge.motion <= 0.0f || nudge.finePitch <= 0.0f)
        return fail ("Auto Nudge residual directions are inconsistent with macro mappings");
    const auto staticNudge = CompareFineTune::suggestFromResidual (residualReference, residualCandidate, staticPatch);
    if (std::abs (staticNudge.motion) > 1.0e-6f)
        return fail ("Auto Nudge suggested motion for a patch without existing motion topology");

    CompareFineTune::Values bright, dark;
    bright.brightness = 0.85f;
    dark.brightness = -0.85f;
    auto brightPatch = CompareFineTune::apply (baseline, bright);
    auto darkPatch = CompareFineTune::apply (baseline, dark);
    brightPatch.layers.fill (nullptr);
    darkPatch.layers.fill (nullptr);

    constexpr double sampleRate = 44100.0;
    constexpr float fundamental = 220.0f;
    const auto brightAudio = OfflineRenderer::renderPatch (brightPatch, sampleRate, 0.75f, fundamental, 128);
    const auto darkAudio = OfflineRenderer::renderPatch (darkPatch, sampleRate, 0.75f, fundamental, 128);
    if (! finiteAudio (brightAudio) || ! finiteAudio (darkAudio))
        return fail ("fine-tune render generated non-finite audio");

    const auto brightFeatures = SampleAnalyzer::analyzeBuffer (brightAudio, sampleRate, fundamental);
    const auto darkFeatures = SampleAnalyzer::analyzeBuffer (darkAudio, sampleRate, fundamental);
    if (brightFeatures.spectralCentroidHz <= darkFeatures.spectralCentroidHz)
        return fail ("brightness macro did not move measured spectral centroid upward");

    std::cout << "Compare fine-tune tests passed. centroid dark=" << darkFeatures.spectralCentroidHz
              << " bright=" << brightFeatures.spectralCentroidHz << '\n';
    return 0;
}
