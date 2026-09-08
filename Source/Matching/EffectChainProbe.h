#pragma once
#include <JuceHeader.h>
#include "../Analysis/SampleAnalyzer.h"
#include "../Engine/SynthEngine.h"
#include <array>
#include <cmath>

struct EffectProbeSignature
{
    static constexpr int detailedBandCount = 8;
    float low = 0.0f, mid = 0.0f, high = 0.0f;
    std::array<float, detailedBandCount> detailedBands {};
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

        float detailSum = 0.0f;
        for (int band = 0; band < EffectProbeSignature::detailedBandCount; ++band)
        {
            float energy = 0.0f;
            const int first = band * SoundFeatures::spectralBandCount / EffectProbeSignature::detailedBandCount;
            const int last = (band + 1) * SoundFeatures::spectralBandCount / EffectProbeSignature::detailedBandCount;
            for (int i = first; i < last; ++i) energy += juce::jmax (0.0f, f.spectralBands[(size_t) i]);
            s.detailedBands[(size_t) band] = energy; detailSum += energy;
        }
        detailSum = juce::jmax (1.0e-6f, detailSum);
        for (auto& energy : s.detailedBands) energy /= detailSum;

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
            ModuleRack voiceRack, globalRack;
            juce::AudioBuffer<float> buffer { 2, totalSamples };
            Context()
            {
                voiceRack.prepare (sampleRate, blockSize, 2);
                globalRack.prepare (sampleRate, blockSize, 2);
            }
        };
        thread_local Context context;

        auto processRack = [&]
        {
            for (int start = 0; start < totalSamples; start += blockSize)
            {
                const int count = juce::jmin (blockSize, totalSamples - start);
                juce::AudioBuffer<float> block (context.buffer.getArrayOfWritePointers(), 2, start, count);
                context.voiceRack.process (block, p.fxModules, 0, p.tempoBpm);
                context.voiceRack.process (block, p.fxModules, 1, p.tempoBpm);
                context.globalRack.process (block, p.globalFxModules, 0, p.tempoBpm);
                context.globalRack.process (block, p.globalFxModules, 1, p.tempoBpm);
            }
        };

        EffectProbeSignature s;

        // Pass 1: identical mono white noise into both channels. Relative low/mid/high
        // output energy estimates the broad colour imposed by HPF/LPF/cab-style stages.
        context.voiceRack.reset(); context.globalRack.reset();
        context.buffer.clear();
        juce::Random random ((int64) 0x524d465850524f42);
        std::array<float, noiseSamples> excitation {};
        for (int i = 0; i < noiseSamples; ++i)
        {
            const float n = random.nextFloat() * 2.0f - 1.0f;
            excitation[(size_t) i] = n;
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

        double detailedSum = 0.0;
        for (int band = 0; band < EffectProbeSignature::detailedBandCount; ++band)
        {
            const double t = (double) band / (double) (EffectProbeSignature::detailedBandCount - 1);
            const double frequency = 70.0 * std::pow (5200.0 / 70.0, t);
            double inRe = 0.0, inIm = 0.0, outRe = 0.0, outIm = 0.0;
            for (int i = 0; i < noiseSamples; ++i)
            {
                const double phase = juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate;
                const double c = std::cos (phase), sn = std::sin (phase);
                const double input = excitation[(size_t) i];
                const double output = 0.5 * ((double) context.buffer.getSample (0, i) + (double) context.buffer.getSample (1, i));
                inRe += input * c; inIm -= input * sn;
                outRe += output * c; outIm -= output * sn;
            }
            const double inputMagnitude = std::sqrt (inRe * inRe + inIm * inIm);
            const double outputMagnitude = std::sqrt (outRe * outRe + outIm * outIm);
            const float transfer = (float) juce::jlimit (0.0, 8.0, outputMagnitude / juce::jmax (1.0e-9, inputMagnitude));
            const float energy = transfer * transfer;
            s.detailedBands[(size_t) band] = energy; detailedSum += energy;
        }
        detailedSum = juce::jmax (1.0e-9, detailedSum);
        for (auto& energy : s.detailedBands) energy = (float) (energy / detailedSum);

        const float activeRms = std::sqrt ((float) (activeSq / noiseSamples));
        const float crest = peak / juce::jmax (1.0e-5f, activeRms);
        s.dynamics = juce::jlimit (0.0f, 1.0f, (crest - 1.0f) / 3.5f);

        // Pass 2: an impulse followed by silence. This exposes echoes, reverb decay and
        // stereo spreading independently from the synthesized note envelope.
        context.voiceRack.reset(); context.globalRack.reset();
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

        // Pass 3: two non-harmonically-related tones expose harmonics and
        // intermodulation generated by saturation/clip/wavefold stages.
        context.voiceRack.reset(); context.globalRack.reset(); context.buffer.clear();
        constexpr double toneA = 233.0, toneB = 617.0;
        constexpr int toneSamples = 4096;
        for (int i = 0; i < toneSamples; ++i)
        {
            const float x = 0.19f * (float) std::sin (juce::MathConstants<double>::twoPi * toneA * i / sampleRate)
                          + 0.19f * (float) std::sin (juce::MathConstants<double>::twoPi * toneB * i / sampleRate);
            context.buffer.setSample (0, i, x); context.buffer.setSample (1, i, x);
        }
        processRack();
        auto magnitudeAt = [&] (double frequency)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < toneSamples; ++i)
            {
                const double x = 0.5 * ((double) context.buffer.getSample (0, i) + (double) context.buffer.getSample (1, i));
                const double phase = juce::MathConstants<double>::twoPi * frequency * i / sampleRate;
                re += x * std::cos (phase); im -= x * std::sin (phase);
            }
            return std::sqrt (re * re + im * im);
        };
        const double fundamentals = magnitudeAt (toneA) + magnitudeAt (toneB);
        const double products = magnitudeAt (toneA * 2.0) + magnitudeAt (toneB * 2.0)
                              + magnitudeAt (toneB - toneA) + magnitudeAt (toneA + toneB)
                              + magnitudeAt (toneB + toneA * 2.0);
        const float measuredNonlinear = juce::jlimit (0.0f, 1.0f, (float) (products / juce::jmax (1.0e-9, fundamentals) * 2.8));
        s.nonlinear = juce::jmax (s.nonlinear, measuredNonlinear);

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
        const float coarseColour = similarity (target.low, candidate.low) * 0.30f
                                 + similarity (target.mid, candidate.mid) * 0.36f
                                 + similarity (target.high, candidate.high) * 0.34f;
        float detailedColour = 0.0f;
        for (int i = 0; i < EffectProbeSignature::detailedBandCount; ++i)
            detailedColour += similarity (target.detailedBands[(size_t) i], candidate.detailedBands[(size_t) i]);
        detailedColour /= (float) EffectProbeSignature::detailedBandCount;
        const float colour = coarseColour * 0.34f + detailedColour * 0.66f;
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
