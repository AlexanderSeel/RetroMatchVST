#pragma once
#include <JuceHeader.h>
#include "../Analysis/SampleAnalyzer.h"
#include "../Engine/SynthEngine.h"
#include <array>
#include <cmath>

struct EffectProbeSignature
{
    float low = 0.0f, mid = 0.0f, high = 0.0f;
    float tail = 0.0f, stereo = 0.0f, nonlinear = 0.0f, dynamics = 0.0f;
};

// Diagnostic excitation for FX-chain matching. White noise estimates broad transfer
// colour; a separate impulse exposes delay/reverb tails. This is deliberately only
// a secondary score because a wet reference does not uniquely reveal its dry source.
class EffectChainProbe
{
public:
    static EffectProbeSignature referenceTarget (const SoundFeatures& f)
    {
        EffectProbeSignature s;
        s.low = juce::jlimit (0.0f, 1.0f, f.lowEnergyRatio);
        s.high = juce::jlimit (0.0f, 1.0f, f.highEnergyRatio);
        s.mid = juce::jmax (0.0f, 1.0f - s.low - s.high);
        const float bandSum = juce::jmax (1.0e-5f, s.low + s.mid + s.high);
        s.low /= bandSum; s.mid /= bandSum; s.high /= bandSum;

        const float releaseContext = f.releaseSeconds / juce::jmax (0.10f, juce::jmin (2.5f, f.duration + 0.15f));
        s.tail = juce::jlimit (0.0f, 1.0f, releaseContext * 1.7f + f.stereoWidth * 0.12f);
        s.stereo = juce::jlimit (0.0f, 1.0f, f.stereoWidth / 1.35f);
        s.nonlinear = juce::jlimit (0.0f, 1.0f,
                                   f.inharmonicity * 1.55f + f.zeroCrossingRate * 0.40f
                                 + juce::jmax (0.0f, f.highEnergyRatio - 0.10f) * (1.0f - f.spectralFlatness) * 0.80f);
        s.dynamics = juce::jlimit (0.0f, 1.0f, f.transientScore * 0.68f + (1.0f - f.sustainLevel) * 0.32f);
        return s;
    }

    static EffectProbeSignature probe (const VoiceParameters& p)
    {
        constexpr double sampleRate = 12000.0;
        constexpr int blockSize = 128;
        constexpr int noiseSamples = 2048;
        constexpr int totalSamples = 8192;

        struct Context
        {
            ModuleRack rack;
            juce::AudioBuffer<float> buffer { 2, totalSamples };
            Context() { rack.prepare (sampleRate, blockSize, 2); }
        };
        thread_local Context context;

        auto processRack = [&]
        {
            for (int start = 0; start < totalSamples; start += blockSize)
            {
                const int count = juce::jmin (blockSize, totalSamples - start);
                juce::AudioBuffer<float> block (context.buffer.getArrayOfWritePointers(), 2, start, count);
                context.rack.process (block, p.fxModules, 0, p.tempoBpm);
                context.rack.process (block, p.fxModules, 1, p.tempoBpm);
            }
        };

        EffectProbeSignature s;

        // Pass 1: identical mono white noise into both channels. Relative low/mid/high
        // output energy estimates the broad colour imposed by HPF/LPF/cab-style stages.
        context.rack.reset();
        context.buffer.clear();
        juce::Random random ((int64) 0x524d465850524f42);
        for (int i = 0; i < noiseSamples; ++i)
        {
            const float n = random.nextFloat() * 2.0f - 1.0f;
            context.buffer.setSample (0, i, n);
            context.buffer.setSample (1, i, n);
        }
        processRack();

        const float lowAlpha = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 260.0f / (float) sampleRate);
        const float midAlpha = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 3200.0f / (float) sampleRate);
        float lowState = 0.0f, midState = 0.0f;
        double lowSq = 0.0, midSq = 0.0, highSq = 0.0, activeSq = 0.0;
        float peak = 0.0f;
        for (int i = 0; i < noiseSamples; ++i)
        {
            const float mono = 0.5f * (context.buffer.getSample (0, i) + context.buffer.getSample (1, i));
            lowState += lowAlpha * (mono - lowState);
            midState += midAlpha * (mono - midState);
            const float low = lowState;
            const float mid = midState - lowState;
            const float high = mono - midState;
            lowSq += (double) low * low;
            midSq += (double) mid * mid;
            highSq += (double) high * high;
            activeSq += (double) mono * mono;
            peak = juce::jmax (peak, std::abs (mono));
        }
        const double bandTotal = juce::jmax (1.0e-12, lowSq + midSq + highSq);
        s.low = (float) (lowSq / bandTotal);
        s.mid = (float) (midSq / bandTotal);
        s.high = (float) (highSq / bandTotal);
        const float activeRms = std::sqrt ((float) (activeSq / noiseSamples));
        const float crest = peak / juce::jmax (1.0e-5f, activeRms);
        s.dynamics = juce::jlimit (0.0f, 1.0f, (crest - 1.0f) / 3.5f);

        // Pass 2: an impulse followed by silence. This exposes echoes, reverb decay and
        // stereo spreading independently from the synthesized note envelope.
        context.rack.reset();
        context.buffer.clear();
        context.buffer.setSample (0, 0, 1.0f);
        context.buffer.setSample (1, 0, 1.0f);
        processRack();

        double directSq = 0.0, tailSq = 0.0, midStereoSq = 0.0, sideSq = 0.0;
        constexpr int directSamples = 96;
        for (int i = 0; i < totalSamples; ++i)
        {
            const float l = context.buffer.getSample (0, i), r = context.buffer.getSample (1, i);
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            if (i < directSamples) directSq += (double) mid * mid;
            else tailSq += (double) mid * mid;
            midStereoSq += (double) mid * mid;
            sideSq += (double) side * side;
        }
        const float directRms = std::sqrt ((float) (directSq / directSamples));
        const float tailRms = std::sqrt ((float) (tailSq / juce::jmax (1, totalSamples - directSamples)));
        s.tail = juce::jlimit (0.0f, 1.0f, tailRms / juce::jmax (1.0e-5f, directRms) * 5.0f);
        const float midRms = std::sqrt ((float) (midStereoSq / totalSamples));
        const float sideRms = std::sqrt ((float) (sideSq / totalSamples));
        s.stereo = juce::jlimit (0.0f, 1.0f, sideRms / juce::jmax (1.0e-5f, midRms + sideRms));

        for (const auto& module : p.fxModules)
        {
            if (module.bypass || module.type == 0) continue;
            if (module.type >= 3 && module.type <= 5)
                s.nonlinear = juce::jmax (s.nonlinear, module.amount * module.mix);
            if (module.type == 13)
                s.dynamics *= 1.0f - juce::jlimit (0.0f, 0.65f, module.amount * module.mix * 0.55f);
            if (module.type == 8 || module.type == 9)
                s.tail = juce::jmax (s.tail, module.mix * (0.35f + module.feedback * 0.45f));
            if (module.type == 6 || module.type == 7)
                s.stereo = juce::jmax (s.stereo, module.mix * module.amount * 0.75f);
        }
        s.nonlinear = juce::jlimit (0.0f, 1.0f, juce::jmax (s.nonlinear, p.drive * 0.75f + p.wavefold * 0.35f));
        return s;
    }

    static float compare (const EffectProbeSignature& target, const EffectProbeSignature& candidate)
    {
        auto similarity = [] (float a, float b) { return juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (a - b)); };
        const float colour = similarity (target.low, candidate.low) * 0.30f
                           + similarity (target.mid, candidate.mid) * 0.36f
                           + similarity (target.high, candidate.high) * 0.34f;
        return juce::jlimit (0.0f, 1.0f,
                            colour * 0.36f
                          + similarity (target.tail, candidate.tail) * 0.20f
                          + similarity (target.stereo, candidate.stereo) * 0.10f
                          + similarity (target.nonlinear, candidate.nonlinear) * 0.20f
                          + similarity (target.dynamics, candidate.dynamics) * 0.14f);
    }

    static float score (const SoundFeatures& reference, const VoiceParameters& candidate)
    {
        return compare (referenceTarget (reference), probe (candidate));
    }
};
