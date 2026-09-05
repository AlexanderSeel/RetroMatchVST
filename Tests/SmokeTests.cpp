#include <JuceHeader.h>
#include "../Source/Analysis/SampleAnalyzer.h"
#include "../Source/Engine/MSEG.h"
#include "../Source/Engine/ReferenceWavetable.h"
#include "../Source/Engine/PresetLibrary.h"
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

bool runMelodyTests();
int main (int argc, char** argv)
{
    if (! runMelodyTests()) return 1;
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
    return 0;
}
