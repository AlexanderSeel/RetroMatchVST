from pathlib import Path


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, text):
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected exactly one marker in {path}, found {count}: {old[:140]!r}")
    write(path, text.replace(old, new, 1))


def replace_all_checked(path, old, new, minimum=1):
    text = read(path)
    count = text.count(old)
    if count < minimum:
        raise RuntimeError(f"expected at least {minimum} markers in {path}, found {count}: {old[:140]!r}")
    write(path, text.replace(old, new))
    return count


def write_new(path, content):
    p = Path(path)
    if p.exists():
        existing = p.read_text(encoding="utf-8")
        if existing == content:
            return
        raise RuntimeError(f"refusing to overwrite unexpected existing file: {path}")
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")


EFFECT_PROBE = r'''#pragma once
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
'''


ADVISOR = r'''#pragma once
#include <JuceHeader.h>
#include "../Analysis/SampleAnalyzer.h"
#include <array>

struct ResynthesisAdvice
{
    int method = 0;       // MatchSettings::algorithm
    int complexity = 0;   // resynthComplexity choice
    float matchability = 0.0f;
    juce::String sourceFamily;
    juce::String reason;

    juce::String methodName() const
    {
        static const juce::StringArray names { "Balanced Hybrid", "Reference Wavetable", "Spectral Subtractive",
                                                "FM / Harmonic", "Layered Studio", "Texture / Chop", "FX / Guitar Chain" };
        return names[juce::jlimit (0, names.size() - 1, method)];
    }
};

class ResynthesisAdvisor
{
public:
    static ResynthesisAdvice advise (const SoundFeatures& f)
    {
        ResynthesisAdvice a;
        const float pitch = juce::jlimit (0.0f, 1.0f, f.pitchConfidence);
        const float harmonic = juce::jlimit (0.0f, 1.0f, f.harmonicity);
        const float flat = juce::jlimit (0.0f, 1.0f, f.spectralFlatness);
        const float transient = juce::jlimit (0.0f, 1.0f, f.transientScore);
        const float motion = juce::jlimit (0.0f, 1.0f, f.spectralMotion * 2.5f);
        const float stereo = juce::jlimit (0.0f, 1.0f, f.stereoWidth / 1.35f);
        const float inharmonic = juce::jlimit (0.0f, 1.0f, f.inharmonicity * 2.0f);
        const float tail = juce::jlimit (0.0f, 1.0f, f.releaseSeconds / juce::jmax (0.18f, juce::jmin (2.5f, f.duration + 0.1f)) * 1.8f);

        const bool guitarLike = pitch > 0.30f && transient > 0.34f && harmonic > 0.26f && flat < 0.50f
                             && f.spectralCentroidHz > 350.0f && f.spectralCentroidHz < 6500.0f
                             && f.spectralRolloffHz > 1200.0f && f.spectralRolloffHz < 16000.0f;

        std::array<float, 7> score {};
        score[0] = 0.46f + pitch * 0.10f + harmonic * 0.08f + transient * 0.05f;
        score[1] = 0.16f + pitch * 0.36f + harmonic * 0.25f + (1.0f - flat) * 0.11f + motion * 0.08f;
        score[2] = 0.18f + pitch * 0.15f + harmonic * 0.18f + (1.0f - motion) * 0.18f + (1.0f - flat) * 0.12f;
        score[3] = 0.14f + pitch * 0.12f + harmonic * 0.28f + inharmonic * 0.24f + f.highEnergyRatio * 0.16f;
        score[4] = 0.20f + pitch * 0.10f + motion * 0.24f + stereo * 0.28f + tail * 0.14f;
        score[5] = 0.14f + flat * 0.22f + motion * 0.30f + (1.0f - pitch) * 0.18f + inharmonic * 0.10f;
        score[6] = 0.15f + pitch * 0.15f + transient * 0.25f + (1.0f - flat) * 0.11f
                         + inharmonic * 0.12f + tail * 0.11f + stereo * 0.08f;
        if (guitarLike) score[6] += 0.24f;

        for (int i = 1; i < (int) score.size(); ++i)
            if (score[(size_t) i] > score[(size_t) a.method]) a.method = i;

        float complexity = motion * 0.95f + stereo * 0.65f + inharmonic * 0.42f + tail * 0.72f
                         + juce::jlimit (0.0f, 0.45f, f.duration / 8.0f);
        a.complexity = complexity > 1.58f ? 3 : (complexity > 1.05f ? 2 : (complexity > 0.55f ? 1 : 0));
        if (a.method == 6) a.complexity = juce::jmax (1, a.complexity);

        const float structure = juce::jmax (pitch, juce::jmax (harmonic, transient));
        a.matchability = juce::jlimit (0.30f, 0.97f,
                                       0.38f + structure * 0.28f + (1.0f - flat) * 0.10f
                                     + juce::jmin (0.12f, motion * 0.12f) + juce::jmin (0.09f, stereo * 0.09f));

        if (guitarLike)
            a.sourceFamily = (inharmonic > 0.28f || tail > 0.22f || stereo > 0.18f)
                               ? "processed pluck / guitar-like" : "clean pluck / guitar-like";
        else if (pitch < 0.22f && flat > 0.42f)
            a.sourceFamily = "noise / texture";
        else if (transient > 0.58f && harmonic < 0.42f)
            a.sourceFamily = "percussive / transient";
        else if (motion > 0.35f)
            a.sourceFamily = "evolving pitched texture";
        else
            a.sourceFamily = "pitched harmonic source";

        switch (a.method)
        {
            case 1: a.reason = "Stable pitch and harmonic structure make cycles extracted from the reference especially useful."; break;
            case 2: a.reason = "The spectrum is comparatively stable, so oscillator plus filter-envelope matching should stay efficient."; break;
            case 3: a.reason = "Harmonic/inharmonic structure favours explicit six-operator reconstruction."; break;
            case 4: a.reason = "Stereo width and spectral motion benefit from complementary layered roles."; break;
            case 5: a.reason = "Texture and time-varying spectrum are stronger than one stable harmonic body."; break;
            case 6: a.reason = "A pitched transient body plus band-limited colour/tail is guitar-like; rebuild the body and search compressor, drive, cabinet filtering, modulation, delay and reverb as an ordered chain."; break;
            default: a.reason = "No single topology dominates; use the hybrid search as the safest first pass."; break;
        }
        return a;
    }
};
'''

write_new("Source/Matching/EffectChainProbe.h", EFFECT_PROBE)
write_new("Source/Matching/ResynthesisAdvisor.h", ADVISOR)

# Match settings/result surface.
replace_once(
    "Source/Matching/SoundMatcher.h",
    "    // 0 balanced hybrid, 1 reference wavetable, 2 subtractive/spectral,\n    // 3 FM/harmonic, 4 layered studio, 5 texture/chopped wavetable.\n",
    "    // 0 balanced hybrid, 1 reference wavetable, 2 subtractive/spectral,\n    // 3 FM/harmonic, 4 layered studio, 5 texture/chopped wavetable,\n    // 6 FX/guitar chain with diagnostic excitation scoring.\n")
replace_once(
    "Source/Matching/SoundMatcher.h",
    "    int evaluatedCandidates = 0;\n    juce::String explanation;\n",
    "    int evaluatedCandidates = 0;\n    float effectProbeSimilarity = -1.0f;\n    juce::String explanation;\n")

# SoundMatcher: seventh topology, white-noise/impulse FX score, and explanation.
replace_once(
    "Source/Matching/SoundMatcher.cpp",
    "#include \"OfflineRenderer.h\"\n",
    "#include \"OfflineRenderer.h\"\n#include \"EffectChainProbe.h\"\n")
replace_once(
    "Source/Matching/SoundMatcher.cpp",
    "    algorithm = juce::jlimit (0, 5, algorithm);\n",
    "    algorithm = juce::jlimit (0, 6, algorithm);\n")

fx_case = r'''
        case 6: // FX / guitar chain: rebuild body first, then an ordered pedal/amp-style chain.
        {
            if (hasReferenceTable)
                p.referenceWavetableMix = juce::jmax (p.referenceWavetableMix,
                    juce::jlimit (0.28f, 0.56f, 0.28f + reference.pitchConfidence * 0.22f + reference.harmonicity * 0.08f));
            p.supersawMix *= 0.35f;
            p.fmMix *= 0.58f;
            p.noiseMix = juce::jmin (0.12f, p.noiseMix);
            p.outputGainDb = juce::jmin (p.outputGainDb, -4.5f);

            if (reference.transientScore > 0.24f)
            {
                p.mseg.enabled = true;
                p.mseg.loopEnabled = false;
                p.msegTarget = (int) ModDestination::amplitude;
                p.msegDepth = juce::jlimit (0.55f, 0.95f, 0.58f + reference.transientScore * 0.34f);
                p.mseg.levels = {{ 0.0f, 1.0f, 0.72f, 0.56f, juce::jlimit (0.18f, 0.72f, reference.sustainLevel), 0.0f }};
                p.mseg.times = {{ juce::jlimit (0.002f, 0.060f, reference.attackSeconds),
                                  juce::jlimit (0.018f, 0.24f, reference.decaySeconds * 0.30f),
                                  juce::jlimit (0.030f, 0.45f, reference.decaySeconds * 0.72f),
                                  juce::jlimit (0.08f, 1.2f, reference.duration * 0.18f),
                                  juce::jlimit (0.04f, 1.8f, reference.releaseSeconds) }};
                p.mseg.curves = {{ -0.42f, 0.18f, -0.08f, 0.06f, -0.24f }};
            }

            auto prime = [] (FxModuleParameters& module, int type, int stage,
                             float amount, float rate, float feedback, float mix)
            {
                if (module.type != type)
                {
                    module = {};
                    module.type = type;
                    module.amount = amount; module.rate = rate; module.feedback = feedback; module.mix = mix;
                }
                else
                {
                    module.amount = juce::jlimit (0.0f, 1.0f, module.amount * 0.78f + amount * 0.22f);
                    module.rate = juce::jlimit (0.0f, 1.0f, module.rate * 0.82f + rate * 0.18f);
                    module.feedback = juce::jlimit (0.0f, 1.0f, module.feedback * 0.82f + feedback * 0.18f);
                    module.mix = juce::jlimit (0.0f, 1.0f, module.mix * 0.82f + mix * 0.18f);
                }
                module.stage = stage;
                module.bypass = false;
            };

            auto amountForCutoff = [] (float hz)
            {
                return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (30.0f, 18000.0f, hz) / 30.0f) / std::log (600.0f));
            };

            const float hpHz = juce::jlimit (45.0f, 180.0f, 55.0f + juce::jmax (0.0f, 0.18f - reference.lowEnergyRatio) * 620.0f);
            const float cabHz = juce::jlimit (2600.0f, 10500.0f,
                                              reference.spectralRolloffHz > 800.0f ? reference.spectralRolloffHz * 0.82f
                                                                                  : reference.spectralCentroidHz * 2.6f + 1900.0f);
            const float compression = juce::jlimit (0.18f, 0.62f, 0.46f - reference.transientScore * 0.18f + reference.sustainLevel * 0.18f);
            const int driveType = reference.inharmonicity > 0.24f && reference.highEnergyRatio > 0.10f ? 4 : 3;
            const float driveAmount = juce::jlimit (0.08f, 0.68f,
                                                    0.08f + reference.inharmonicity * 1.25f
                                                          + juce::jmax (0.0f, reference.highEnergyRatio - 0.08f) * 1.10f);

            prime (p.fxModules[0], 2, 0, amountForCutoff (hpHz), 0.0f, 0.06f, 1.0f);                    // HPF
            prime (p.fxModules[1], 13, 0, compression, 0.05f + reference.transientScore * 0.10f,
                   0.20f + reference.sustainLevel * 0.24f, 0.72f);                                      // compressor
            prime (p.fxModules[2], driveType, 0, driveAmount,
                   juce::jlimit (0.18f, 0.86f, 0.60f - reference.highEnergyRatio * 0.55f), 0.50f,
                   juce::jlimit (0.32f, 0.92f, 0.38f + driveAmount * 0.72f));                            // drive
            prime (p.fxModules[3], 1, 0, amountForCutoff (cabHz), 0.0f, 0.05f, 1.0f);                    // cab / speaker LPF

            const bool wantsModulation = reference.stereoWidth > 0.16f || reference.spectralMotion > 0.055f;
            if (wantsModulation)
                prime (p.fxModules[4], 6, 1,
                       juce::jlimit (0.08f, 0.42f, 0.08f + reference.stereoWidth * 0.26f),
                       juce::jlimit (0.04f, 0.35f, 0.08f + reference.spectralMotion * 1.5f), 0.08f,
                       juce::jlimit (0.06f, 0.28f, 0.06f + reference.stereoWidth * 0.18f));
            else p.fxModules[4] = {};

            const bool wantsDelay = reference.releaseSeconds > 0.20f && (reference.stereoWidth > 0.10f || reference.duration > 0.70f);
            if (wantsDelay)
            {
                const float delaySeconds = juce::jlimit (0.09f, 0.52f, 0.11f + reference.releaseSeconds * 0.22f + reference.spectralMotion * 0.70f);
                prime (p.fxModules[5], 8, 1, (delaySeconds - 0.01f) / 1.99f, 0.58f,
                       juce::jlimit (0.10f, 0.58f, 0.12f + reference.releaseSeconds * 0.26f),
                       juce::jlimit (0.06f, 0.34f, 0.07f + reference.stereoWidth * 0.14f + reference.releaseSeconds * 0.10f));
            }
            else p.fxModules[5] = {};

            const bool wantsReverb = reference.releaseSeconds > 0.16f || reference.stereoWidth > 0.16f;
            if (wantsReverb)
                prime (p.fxModules[6], 9, 1,
                       juce::jlimit (0.14f, 0.72f, 0.18f + reference.releaseSeconds * 0.30f),
                       juce::jlimit (0.18f, 0.82f, 0.62f - reference.highEnergyRatio * 0.55f),
                       juce::jlimit (0.18f, 0.92f, 0.30f + reference.stereoWidth * 0.48f),
                       juce::jlimit (0.06f, 0.38f, 0.08f + reference.releaseSeconds * 0.13f + reference.stereoWidth * 0.12f));
            else p.fxModules[6] = {};
            p.fxModules[7] = {};

            // Keep this strategy's effects in one inspectable modular chain so the
            // white-noise/impulse probe measures the same chain the user sees in FX RACK.
            p.drive = 0.0f; p.chorusMix = 0.0f; p.delayMix = 0.0f; p.reverbMix = 0.0f;
            p.stereoWidth = juce::jmax (1.0f, p.stereoWidth);
            break;
        }

'''
replace_once(
    "Source/Matching/SoundMatcher.cpp",
    "        default: // Balanced hybrid: use a reference table whenever it is meaningful.\n",
    fx_case + "        default: // Balanced hybrid: use a reference table whenever it is meaningful.\n")
replace_once(
    "Source/Matching/SoundMatcher.cpp",
    "    result.similarity = SimilarityScorer::compare (reference, result.candidateFeatures);\n    result.confidence = result.similarity.total;\n",
    "    result.similarity = SimilarityScorer::compare (reference, result.candidateFeatures);\n"
    "    if (settings.algorithm == 6)\n"
    "    {\n"
    "        result.effectProbeSimilarity = EffectChainProbe::score (reference, params);\n"
    "        // The musical render remains dominant. The probe only separates effect-chain\n"
    "        // colour/tail candidates that can look similar from one played note.\n"
    "        result.similarity.total = juce::jlimit (0.0f, 1.0f, result.similarity.total * 0.86f + result.effectProbeSimilarity * 0.14f);\n"
    "    }\n"
    "    result.confidence = result.similarity.total;\n")
replace_once(
    "Source/Matching/SoundMatcher.cpp",
    "    best.explanation = \"Population closed-loop match: candidate patches are rendered, re-analysed, ranked and evolved across VA, wavetable, supersaw/unison, wavefolding, additive and six-operator FM (including operator envelopes/fixed modes) plus modulation-route topology, independent LFOs, MSEG shapes and modular pre/post effects against global spectrum, time-varying spectrum, cepstral timbre, envelope, harmonic, pitch and stereo descriptors.\";\n    if (progress) progress (1.0f);\n",
    "    best.explanation = \"Population closed-loop match: candidate patches are rendered, re-analysed, ranked and evolved across VA, wavetable, supersaw/unison, wavefolding, additive and six-operator FM (including operator envelopes/fixed modes) plus modulation-route topology, independent LFOs, MSEG shapes and modular pre/post effects against global spectrum, time-varying spectrum, cepstral timbre, envelope, harmonic, pitch and stereo descriptors.\";\n"
    "    if (settings.algorithm == 6)\n"
    "        best.explanation += \" FX / Guitar Chain additionally compares deterministic white-noise transfer colour and impulse-tail behaviour, while keeping that diagnostic score secondary to the rendered musical match.\";\n"
    "    if (progress) progress (1.0f);\n")

# Processor: expose seventh choice, use some reference body, and keep companion FX conservative.
replace_once(
    "Source/PluginProcessor.cpp",
    "    if (strategy == 5) weight = juce::jmax (0.62f, weight);\n",
    "    if (strategy == 5) weight = juce::jmax (0.62f, weight);\n    if (strategy == 6) weight = juce::jmax (0.34f, weight * 0.78f);\n")
replace_once(
    "Source/PluginProcessor.cpp",
    "    p.delayMix *= 0.65f; p.reverbMix *= 0.75f;\n\n    switch (role % 7)\n",
    "    p.delayMix *= 0.65f; p.reverbMix *= 0.75f;\n"
    "    if (strategy == 6)\n"
    "    {\n"
    "        // The main guitar-like voice owns the audible pedal/amp chain. Companion\n"
    "        // layers contribute body/air/foundation without multiplying every tail.\n"
    "        for (auto& module : p.fxModules)\n"
    "            if (module.type == 6 || module.type == 7 || module.type == 8 || module.type == 9) module.mix *= 0.32f;\n"
    "            else module.mix *= 0.68f;\n"
    "        p.chorusMix = p.delayMix = p.reverbMix = 0.0f;\n"
    "    }\n\n"
    "    switch (role % 7)\n")
replace_all_checked(
    "Source/PluginProcessor.cpp",
    "juce::jlimit (0, 5, (int) apvts.getRawParameterValue (\"resynthStrategy\")->load())",
    "juce::jlimit (0, 6, (int) apvts.getRawParameterValue (\"resynthStrategy\")->load())",
    minimum=2)
replace_once(
    "Source/PluginProcessor.cpp",
    "        juce::StringArray { \"Balanced Hybrid\", \"Reference Wavetable\", \"Spectral Subtractive\",\n                            \"FM / Harmonic\", \"Layered Studio\", \"Texture / Chop\" }, 0));\n",
    "        juce::StringArray { \"Balanced Hybrid\", \"Reference Wavetable\", \"Spectral Subtractive\",\n                            \"FM / Harmonic\", \"Layered Studio\", \"Texture / Chop\", \"FX / Guitar Chain\" }, 0));\n")

# Main editor: advisor line and seventh strategy.
replace_once(
    "Source/UI/RetroMatchEditorV3.h",
    "#include \"../AI/AISeedProvider.h\"\n",
    "#include \"../AI/AISeedProvider.h\"\n#include \"../Matching/ResynthesisAdvisor.h\"\n")
replace_once(
    "Source/UI/RetroMatchEditorV3.h",
    "    juce::Label resynthStrategyLabel, resynthComplexityLabel;\n",
    "    juce::Label resynthStrategyLabel, resynthComplexityLabel, resynthAdvice;\n")
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    resynthStrategyChoice.addItemList ({ \"Balanced Hybrid\", \"Reference Wavetable\", \"Spectral Subtractive\",\n                                         \"FM / Harmonic\", \"Layered Studio\", \"Texture / Chop\" }, 1);\n",
    "    resynthStrategyChoice.addItemList ({ \"Balanced Hybrid\", \"Reference Wavetable\", \"Spectral Subtractive\",\n                                         \"FM / Harmonic\", \"Layered Studio\", \"Texture / Chop\", \"FX / Guitar Chain\" }, 1);\n")
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    resynthStrategyChoice.setTooltip (\"Resynthesis / matching topology used by Quick and Refine. Reference Wavetable and Texture / Chop deliberately use more of the loaded sample.\");\n",
    "    resynthStrategyChoice.setTooltip (\"Resynthesis / matching topology used by Quick and Refine. Reference Wavetable and Texture / Chop use the sample directly; FX / Guitar Chain searches an ordered compressor/drive/cab/mod/delay/reverb rack and adds white-noise/impulse diagnostic scoring.\");\n")
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    for (auto* c : std::array<juce::Component*, 4> { &resynthStrategyLabel, &resynthStrategyChoice, &resynthComplexityLabel, &resynthComplexityChoice })\n        addAndMakeVisible (*c);\n\n    load.onClick",
    "    for (auto* c : std::array<juce::Component*, 4> { &resynthStrategyLabel, &resynthStrategyChoice, &resynthComplexityLabel, &resynthComplexityChoice })\n        addAndMakeVisible (*c);\n"
    "    resynthAdvice.setText (\"ADVISOR  load a reference\", juce::dontSendNotification);\n"
    "    resynthAdvice.setJustificationType (juce::Justification::centredLeft);\n"
    "    resynthAdvice.setColour (juce::Label::textColourId, tealColour (*this));\n"
    "    resynthAdvice.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0b1416));\n"
    "    resynthAdvice.setFont (juce::Font (juce::FontOptions (8.8f, juce::Font::bold)));\n"
    "    resynthAdvice.setMinimumHorizontalScale (0.66f);\n"
    "    addAndMakeVisible (resynthAdvice);\n\n"
    "    load.onClick")
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    resynthComplexityChoice.setBounds (depthRow.reduced (2, 1));\n    w.removeFromTop (5);\n\n    referencePitchInfo.setBounds",
    "    resynthComplexityChoice.setBounds (depthRow.reduced (2, 1));\n"
    "    w.removeFromTop (3);\n"
    "    resynthAdvice.setBounds (w.removeFromTop (24).reduced (2, 1));\n"
    "    w.removeFromTop (4);\n\n"
    "    referencePitchInfo.setBounds")
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "    updateTypingKeyboard(); proc.refreshEditingLayer();\n    const int selected = proc.getEditingLayer();\n",
    "    updateTypingKeyboard(); proc.refreshEditingLayer();\n"
    "    if (proc.currentFeatures)\n"
    "    {\n"
    "        const auto advice = ResynthesisAdvisor::advise (*proc.currentFeatures);\n"
    "        static const juce::StringArray depths { \"CLASSIC 1-3\", \"STUDIO 4\", \"DEEP 6\", \"MAX 8\" };\n"
    "        resynthAdvice.setText (\"ADVISOR  \" + advice.methodName().toUpperCase() + \"  •  \"\n"
    "                               + depths[juce::jlimit (0, depths.size() - 1, advice.complexity)] + \"  •  \"\n"
    "                               + juce::String (advice.matchability * 100.0f, 0) + \"%\", juce::dontSendNotification);\n"
    "        resynthAdvice.setTooltip (advice.sourceFamily + \" — \" + advice.reason);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        resynthAdvice.setText (\"ADVISOR  load a reference\", juce::dontSendNotification);\n"
    "        resynthAdvice.setTooltip (\"RetroMatch ranks synthesis topology and stack depth from the measured reference features.\");\n"
    "    }\n"
    "    const int selected = proc.getEditingLayer();\n")
replace_all_checked(
    "Source/UI/RetroMatchEditorV3.cpp",
    "juce::jlimit (0, 5, (int) paramValue (proc, \"resynthStrategy\", 0))",
    "juce::jlimit (0, 6, (int) paramValue (proc, \"resynthStrategy\", 0))",
    minimum=1)
replace_once(
    "Source/UI/RetroMatchEditorV3.cpp",
    "            if (strategy == 1) target = 0.76f; else if (strategy == 2) target = 0.05f; else if (strategy == 3) target = 0.14f; else if (strategy == 4) target = 0.42f; else if (strategy == 5) target = 0.68f;\n",
    "            if (strategy == 1) target = 0.76f; else if (strategy == 2) target = 0.05f; else if (strategy == 3) target = 0.14f; else if (strategy == 4) target = 0.42f; else if (strategy == 5) target = 0.68f; else if (strategy == 6) target = 0.36f;\n")

# Other method selectors/readouts.
replace_once(
    "Source/UI/LayersPage.h",
    "        addAndMakeVisible (strategy); strategy.addItemList ({ \"BALANCED HYBRID\", \"REFERENCE WAVETABLE\", \"SPECTRAL SUBTRACTIVE\", \"FM / HARMONIC\", \"LAYERED STUDIO\", \"TEXTURE / CHOP\" }, 1);\n",
    "        addAndMakeVisible (strategy); strategy.addItemList ({ \"BALANCED HYBRID\", \"REFERENCE WAVETABLE\", \"SPECTRAL SUBTRACTIVE\", \"FM / HARMONIC\", \"LAYERED STUDIO\", \"TEXTURE / CHOP\", \"FX / GUITAR CHAIN\" }, 1);\n")
replace_once(
    "Source/UI/LayersPage.h",
    "        strategy.setTooltip (\"Changes the search topology. Reference Wavetable and Texture/Chop deliberately lean on cycles extracted from the selected sample region.\");\n",
    "        strategy.setTooltip (\"Changes the search topology. Reference Wavetable and Texture/Chop lean on extracted cycles; FX/Guitar Chain rebuilds an inspectable ordered pedal/amp-style rack and uses diagnostic excitation scoring.\");\n")
for path in ["Source/UI/ReferenceEditorDialog.h", "Source/UI/MatchCompareDialog.h"]:
    replace_once(
        path,
        "        const juce::StringArray methods { \"Balanced Hybrid\", \"Reference Wavetable\", \"Spectral Subtractive\",\n                                           \"FM / Harmonic\", \"Layered Studio\", \"Texture / Chop\" };\n",
        "        const juce::StringArray methods { \"Balanced Hybrid\", \"Reference Wavetable\", \"Spectral Subtractive\",\n                                           \"FM / Harmonic\", \"Layered Studio\", \"Texture / Chop\", \"FX / Guitar Chain\" };\n")

# Compare dialog: expose the diagnostic score in the last metric cell when relevant.
replace_once(
    "Source/UI/MatchCompareDialog.h",
    "        const std::array<std::pair<const char*, float>, 8> values {{\n            { \"TOTAL\", s.total }, { \"SPECTRUM\", s.spectrum }, { \"TIMBRE\", s.timbre }, { \"TEMPORAL\", s.temporal },\n            { \"HARMONIC\", s.harmonic }, { \"ENVELOPE\", s.envelope }, { \"PITCH\", s.pitch }, { \"STEREO\", s.stereo }\n        }};\n",
    "        const bool hasFxProbe = proc.lastMatch.effectProbeSimilarity >= 0.0f;\n"
    "        const std::array<std::pair<const char*, float>, 8> values {{\n"
    "            { \"TOTAL\", s.total }, { \"SPECTRUM\", s.spectrum }, { \"TIMBRE\", s.timbre }, { \"TEMPORAL\", s.temporal },\n"
    "            { \"HARMONIC\", s.harmonic }, { \"ENVELOPE\", s.envelope }, { \"STEREO\", s.stereo },\n"
    "            { hasFxProbe ? \"FX PROBE\" : \"PITCH\", hasFxProbe ? proc.lastMatch.effectProbeSimilarity : s.pitch }\n"
    "        }};\n")

# Smoke test: advisor recommendation and algorithm-6 diagnostic plumbing.
replace_once(
    "Tests/SmokeTests.cpp",
    "#include \"../Source/Matching/SoundMatcher.h\"\n",
    "#include \"../Source/Matching/SoundMatcher.h\"\n#include \"../Source/Matching/EffectChainProbe.h\"\n#include \"../Source/Matching/ResynthesisAdvisor.h\"\n")
replace_once(
    "Tests/SmokeTests.cpp",
    "    if (! runMelodyTests()) return 1;\n    {\n        VoiceParameters tone;",
    "    if (! runMelodyTests()) return 1;\n"
    "    {\n"
    "        SoundFeatures guitar;\n"
    "        guitar.duration = 1.8f; guitar.fundamentalHz = 110.0f; guitar.pitchConfidence = 0.84f;\n"
    "        guitar.harmonicity = 0.68f; guitar.inharmonicity = 0.16f; guitar.transientScore = 0.72f;\n"
    "        guitar.spectralFlatness = 0.12f; guitar.spectralCentroidHz = 1850.0f; guitar.spectralRolloffHz = 6200.0f;\n"
    "        guitar.lowEnergyRatio = 0.20f; guitar.highEnergyRatio = 0.16f; guitar.zeroCrossingRate = 0.08f;\n"
    "        guitar.attackSeconds = 0.004f; guitar.decaySeconds = 0.34f; guitar.sustainLevel = 0.46f; guitar.releaseSeconds = 0.52f;\n"
    "        guitar.stereoWidth = 0.34f; guitar.spectralMotion = 0.08f;\n"
    "        const auto advice = ResynthesisAdvisor::advise (guitar);\n"
    "        if (advice.method != 6 || advice.complexity < 1) return fail (\"guitar-like reference was not routed to FX / Guitar Chain\");\n"
    "        auto seed = SoundMatcher::initialFit (guitar).params;\n"
    "        MatchSettings fxSettings; fxSettings.algorithm = 6; fxSettings.iterations = 0; fxSettings.topologyTrials = 0;\n"
    "        fxSettings.populationSize = 2; fxSettings.renderSampleRate = 12000.0; fxSettings.maxRenderSeconds = 0.5f;\n"
    "        const auto fxMatch = SoundMatcher::refineFit (guitar, seed, fxSettings);\n"
    "        if (! std::isfinite (fxMatch.effectProbeSimilarity) || fxMatch.effectProbeSimilarity < 0.0f || fxMatch.effectProbeSimilarity > 1.0f)\n"
    "            return fail (\"FX / Guitar Chain diagnostic probe score invalid\");\n"
    "    }\n"
    "    {\n"
    "        VoiceParameters tone;")

# Guardrails for migration completeness.
checks = {
    "Source/Matching/EffectChainProbe.h": ["white noise", "impulse", "EffectChainProbe"],
    "Source/Matching/ResynthesisAdvisor.h": ["FX / Guitar Chain", "processed pluck / guitar-like"],
    "Source/Matching/SoundMatcher.cpp": ["case 6: // FX / guitar chain", "EffectChainProbe::score"],
    "Source/PluginProcessor.cpp": ["FX / Guitar Chain"],
    "Source/UI/RetroMatchEditorV3.cpp": ["ADVISOR", "FX / Guitar Chain"],
    "Tests/SmokeTests.cpp": ["guitar-like reference was not routed"]
}
for path, needles in checks.items():
    text = read(path)
    for needle in needles:
        if needle not in text:
            raise RuntimeError(f"post-migration check failed: {needle!r} missing from {path}")

print("FX/guitar resynthesis advisor migration applied")
