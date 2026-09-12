#pragma once

#include "../Analysis/SampleAnalyzer.h"
#include "../Sequencer/StepSequencer.h"

struct SequencerInference
{
    bool useful = false;
    float confidence = 0.0f;
    juce::String reason;
    RetroMatchSequencer::Settings settings {};
    std::array<RetroMatchSequencer::Step, RetroMatchSequencer::maxSteps> steps {};

    static SequencerInference fromReference (const SoundFeatures& f)
    {
        SequencerInference result;
        float rmsMin = 1.0f, rmsMax = 0.0f;
        for (const auto value : f.temporalRms) { rmsMin = juce::jmin (rmsMin, value); rmsMax = juce::jmax (rmsMax, value); }
        const float envelopeMotion = rmsMax - rmsMin;
        const float spectralMotion = juce::jlimit (0.0f, 1.0f, f.spectralMotion * 2.5f);
        const bool oneShotLike = f.transientScore > 0.76f && f.sustainLevel < 0.36f;
        result.confidence = juce::jlimit (0.0f, 1.0f, spectralMotion * 0.62f + envelopeMotion * 0.48f);
        result.useful = ! oneShotLike && f.duration >= 0.55f && result.confidence >= 0.13f;
        result.settings.enabled = false; // Analysis suggests; the user explicitly arms it.
        result.settings.clockSource = RetroMatchSequencer::ClockSource::host;
        result.settings.restartMode = RetroMatchSequencer::RestartMode::firstNote;
        result.settings.mode = RetroMatchSequencer::Mode::pattern;
        result.settings.outputMode = RetroMatchSequencer::OutputMode::motionOnly;
        result.settings.targetScope = RetroMatchSequencer::TargetScope::mainInstance;
        result.settings.length = 16;
        result.settings.division = f.duration > 3.0f ? RetroMatchSequencer::Division::eighth : RetroMatchSequencer::Division::sixteenth;
        result.settings.macroDestination = {{ RetroMatchSequencer::MacroDestination::cutoff, RetroMatchSequencer::MacroDestination::wavetablePosition }};
        result.settings.macroInterpolation = {{ RetroMatchSequencer::MacroInterpolation::smooth, RetroMatchSequencer::MacroInterpolation::linear }};
        result.settings.macroLaneRate = {{ 1.0f, 0.5f }};
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i)
        {
            auto& step = result.steps[(size_t) i];
            step.rest = i >= result.settings.length;
            const int frame = juce::jmin (SoundFeatures::temporalFrameCount - 1, i * SoundFeatures::temporalFrameCount / result.settings.length);
            const float next = juce::jmin (SoundFeatures::temporalFrameCount - 1, frame + 1);
            const float t = (i * SoundFeatures::temporalFrameCount / (float) result.settings.length) - frame;
            const float rms = f.temporalRms[(size_t) frame] + (f.temporalRms[(size_t) next] - f.temporalRms[(size_t) frame]) * t;
            const float high = f.temporalSpectralBands[(size_t) frame][SoundFeatures::temporalBandCount - 1];
            step.macro = {{ juce::jlimit (0.0f, 1.0f, 0.18f + rms * 0.82f), juce::jlimit (0.0f, 1.0f, 0.16f + high * 0.84f) }};
            step.velocity = juce::jlimit (0.35f, 1.0f, 0.55f + rms * 0.35f);
            step.gate = 0.9f;
        }
        result.reason = result.useful
            ? "Temporal RMS/spectral motion supports a note-gated motion-only reconstruction suggestion."
            : "The reference is too static, too short or too one-shot-like for a sequencer hypothesis.";
        return result;
    }
};
