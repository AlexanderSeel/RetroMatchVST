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
    if (std::abs (neutral.cutoff - baseline.cutoff) > 1.0e-6f
        || std::abs (neutral.attack - baseline.attack) > 1.0e-6f
        || std::abs (neutral.stereoWidth - baseline.stereoWidth) > 1.0e-6f
        || ! neutral.layers[0]
        || std::abs (neutral.layers[0]->cutoff - baseline.layers[0]->cutoff) > 1.0e-6f)
        return fail ("neutral compare fine-tune changed the baseline");

    CompareFineTune::Values positive;
    positive.brightness = 0.75f;
    positive.lowEnd = 0.50f;
    positive.punch = 0.70f;
    positive.tail = 0.60f;
    positive.width = 0.70f;
    positive.motion = 0.65f;
    positive.finePitch = 0.40f;
    const auto adjusted = CompareFineTune::apply (baseline, positive);

    if (adjusted.cutoff <= baseline.cutoff) return fail ("brightness did not raise cutoff");
    if (adjusted.subMix <= baseline.subMix) return fail ("low-end did not raise sub balance");
    if (adjusted.attack >= baseline.attack) return fail ("punch did not shorten attack");
    if (adjusted.release <= baseline.release) return fail ("tail did not lengthen release");
    if (adjusted.stereoWidth <= baseline.stereoWidth || adjusted.layerPan[0] <= baseline.layerPan[0])
        return fail ("width did not widen voice/layer image");
    if (adjusted.modGraphSlots[0].amount <= baseline.modGraphSlots[0].amount
        || adjusted.msegDepth <= baseline.msegDepth)
        return fail ("motion did not scale existing modulation");
    if (std::abs (adjusted.masterTuneCents - 20.0f) > 0.01f)
        return fail ("fine-pitch mapping is incorrect");
    if (! adjusted.layers[0] || adjusted.layers[0].get() == baseline.layers[0].get())
        return fail ("full-rack fine-tune did not clone immutable layer state");

    CompareFineTune::Values staticMotion;
    staticMotion.motion = 1.0f;
    VoiceParameters staticPatch;
    staticPatch.mseg.enabled = false;
    staticPatch.msegDepth = 0.0f;
    const auto stillStatic = CompareFineTune::apply (staticPatch, staticMotion);
    if (stillStatic.mseg.enabled || std::abs (stillStatic.lfoPitch) > 1.0e-6f
        || std::abs (stillStatic.lfoCutoff) > 1.0e-6f || std::abs (stillStatic.lfoAmp) > 1.0e-6f)
        return fail ("motion macro invented modulation topology");

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
