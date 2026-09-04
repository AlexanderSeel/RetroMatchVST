#include "SynthEngine.h"
#include <cmath>

namespace
{
int qualityIndex (int quality)
{
    return juce::jlimit (0, 2, quality);
}
}

HybridVoice::HybridVoice() = default;

void HybridVoice::prepare (double sampleRate, int samplesPerBlock, int channels)
{
    sr = sampleRate;
    const auto safeChannels = (juce::uint32) juce::jmax (1, channels);
    const auto safeBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    juce::dsp::ProcessSpec spec { sampleRate, safeBlockSize, safeChannels };
    filter.prepare (spec);
    filter.reset();
    ampEnv.setSampleRate (sampleRate);
    for (auto& env : fmEnvelopes) env.setSampleRate (sampleRate);

    oversampling2x = std::make_unique<juce::dsp::Oversampling<float>> (
        safeChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling4x = std::make_unique<juce::dsp::Oversampling<float>> (
        safeChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling2x->initProcessing (safeBlockSize);
    oversampling4x->initProcessing (safeBlockSize);
    oversampling2x->reset();
    oversampling4x->reset();

    renderScratch.setSize ((int) safeChannels, (int) safeBlockSize, false, false, true);
    cutoffScratch.resize ((size_t) safeBlockSize, 12000.0f);
    gainScratch.resize ((size_t) safeBlockSize, 0.0f);
    wavefoldScratch.resize ((size_t) safeBlockSize, 0.0f);
}

float HybridVoice::getOversamplingLatencySamples (int quality) const noexcept
{
    switch (qualityIndex (quality))
    {
        case 1: return oversampling2x != nullptr ? oversampling2x->getLatencyInSamples() : 0.0f;
        case 2: return oversampling4x != nullptr ? oversampling4x->getLatencyInSamples() : 0.0f;
        default: return 0.0f;
    }
}

void HybridVoice::setParameters (const VoiceParameters& p)
{
    params = p;
    envParams.attack = juce::jmax (0.001f, p.attack);
    envParams.decay = juce::jmax (0.001f, p.decay);
    envParams.sustain = juce::jlimit (0.0f, 1.0f, p.sustain);
    envParams.release = juce::jmax (0.001f, p.release);
    ampEnv.setParameters (envParams);

    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        auto& e = fmEnvelopeParams[(size_t) i];
        e.attack = juce::jmax (0.001f, p.fmOpAttack[(size_t) i]);
        e.decay = juce::jmax (0.001f, p.fmOpDecay[(size_t) i]);
        e.sustain = juce::jlimit (0.0f, 1.0f, p.fmOpSustain[(size_t) i]);
        e.release = juce::jmax (0.001f, p.fmOpRelease[(size_t) i]);
        fmEnvelopes[(size_t) i].setParameters (e);
    }

    filter.setResonance (0.55f + juce::jlimit (0.0f, 1.0f, p.resonance) * 11.5f);
    filter.setType (p.filterType == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                                     : p.filterType == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                                         : juce::dsp::StateVariableTPTFilterType::lowpass);
}

float HybridVoice::polyBlep (double phase, double dt)
{
    if (dt <= 0.0) return 0.0f;
    if (phase < dt)
    {
        const auto t = phase / dt;
        return (float) (t + t - t * t - 1.0);
    }
    if (phase > 1.0 - dt)
    {
        const auto t = (phase - 1.0) / dt;
        return (float) (t * t + t + t + 1.0);
    }
    return 0.0f;
}

float HybridVoice::wave (int type, double p, double dt, float pulseWidth)
{
    p -= std::floor (p);
    pulseWidth = juce::jlimit (0.05f, 0.95f, pulseWidth);

    switch (type)
    {
        case 0: return std::sin ((float) (juce::MathConstants<double>::twoPi * p));
        case 1:
        {
            auto y = (float) (2.0 * p - 1.0);
            y -= polyBlep (p, dt);
            return y;
        }
        case 2:
        case 4:
        {
            const auto pw = type == 2 ? 0.5 : (double) pulseWidth;
            auto y = p < pw ? 1.0f : -1.0f;
            y += polyBlep (p, dt);
            auto shifted = p - pw;
            if (shifted < 0.0) shifted += 1.0;
            y -= polyBlep (shifted, dt);
            return y;
        }
        case 3: return (float) (1.0 - 4.0 * std::abs (p - 0.5));
        default: return 0.0f;
    }
}

float HybridVoice::wavetableWave (double phase, float position, float warp)
{
    using Frame = std::array<float, wavetableSize>;
    using Bank = std::array<Frame, wavetableFrameCount>;
    static const Bank bank = []
    {
        Bank b {};
        for (int i = 0; i < wavetableSize; ++i)
        {
            const double p = i / (double) wavetableSize;
            const float a = (float) (juce::MathConstants<double>::twoPi * p);
            b[0][(size_t) i] = std::sin (a);
            b[1][(size_t) i] = (float) (1.0 - 4.0 * std::abs (p - 0.5));
            b[2][(size_t) i] = (float) (2.0 * p - 1.0);
            b[3][(size_t) i] = p < 0.5 ? 1.0f : -1.0f;
            b[4][(size_t) i] = (std::sin (a) + 0.68f * std::sin (2.0f * a) + 0.42f * std::sin (5.0f * a) + 0.24f * std::sin (9.0f * a)) / 2.34f;
        }
        return b;
    }();

    double p = phase - std::floor (phase);
    const float w = juce::jlimit (-1.0f, 1.0f, warp);
    if (w > 0.0001f) p = std::pow (p, 1.0 + w * 2.5);
    else if (w < -0.0001f) p = 1.0 - std::pow (1.0 - p, 1.0 + (-w) * 2.5);

    const float framePos = juce::jlimit (0.0f, 1.0f, position) * (wavetableFrameCount - 1);
    const int frame0 = juce::jlimit (0, wavetableFrameCount - 1, (int) std::floor (framePos));
    const int frame1 = juce::jmin (wavetableFrameCount - 1, frame0 + 1);
    const float frameFrac = framePos - frame0;

    const double tablePos = p * wavetableSize;
    const int i0 = ((int) tablePos) % wavetableSize;
    const int i1 = (i0 + 1) % wavetableSize;
    const float frac = (float) (tablePos - std::floor (tablePos));
    auto sampleFrame = [&] (int frame)
    {
        return juce::jmap (frac, bank[(size_t) frame][(size_t) i0], bank[(size_t) frame][(size_t) i1]);
    };
    return juce::jmap (frameFrac, sampleFrame (frame0), sampleFrame (frame1));
}

float HybridVoice::foldSample (float sample, float amount)
{
    const float a = juce::jlimit (0.0f, 1.0f, amount);
    if (a <= 0.0001f) return sample;
    const float driven = sample * (1.0f + a * 8.0f);
    const float halfPi = juce::MathConstants<float>::pi * 0.5f;
    return std::asin (std::sin (driven * halfPi)) / halfPi;
}

void HybridVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    baseHz = (float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    level = velocity;
    phase1 = phase2 = subPhase = lfoPhase = 0.0;
    fmPhase.fill (0.0);
    for (size_t i = 0; i < unisonPhase.size(); ++i)
        unisonPhase[i] = (double) i / unisonPhase.size() * 0.17;
    fmFeedbackState = 0.0f;
    currentMidiNote = midiNoteNumber;
    randomNoteValue = random.nextFloat() * 2.0f - 1.0f;
    ampEnv.noteOn();
    for (auto& env : fmEnvelopes) env.noteOn();
}

void HybridVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampEnv.noteOff();
        for (auto& env : fmEnvelopes) env.noteOff();
    }
    else
    {
        ampEnv.reset();
        for (auto& env : fmEnvelopes) env.reset();
        clearCurrentNote();
    }
}

void HybridVoice::pitchWheelMoved (int newValue)
{
    pitchWheelSemitones = juce::jmap ((float) newValue, 0.0f, 16383.0f, -2.0f, 2.0f);
}

float HybridVoice::getModSourceValue (int source, float lfo, float envelopeValue) const
{
    switch ((ModSource) source)
    {
        case ModSource::none:        return 0.0f;
        case ModSource::lfo1:         return lfo;
        case ModSource::velocity:     return level * 2.0f - 1.0f;
        case ModSource::keyTrack:     return juce::jlimit (-1.0f, 1.0f, (currentMidiNote - 60) / 36.0f);
        case ModSource::randomNote:   return randomNoteValue;
        case ModSource::ampEnvelope: return envelopeValue * 2.0f - 1.0f;
        default:                      return 0.0f;
    }
}

float HybridVoice::renderSixOperatorFm (float fundamentalHz)
{
    std::array<float, VoiceParameters::fmOperatorCount> op {};
    std::array<float, VoiceParameters::fmOperatorCount> envValue {};
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
        envValue[(size_t) i] = fmEnvelopes[(size_t) i].getNextSample();

    auto eval = [&] (int index, float phaseMod)
    {
        const auto ix = (size_t) index;
        const float levelValue = juce::jlimit (0.0f, 1.25f, params.fmOpLevel[ix]);
        const float keyGain = std::pow (2.0f, -juce::jlimit (0.0f, 1.0f, params.fmOpKeyScale[ix]) * std::abs (currentMidiNote - 60) / 24.0f);
        const float velSensitivity = juce::jlimit (0.0f, 1.0f, params.fmOpVelocity[ix]);
        const float velocityGain = 1.0f + velSensitivity * (juce::jlimit (0.0f, 1.0f, level) - 1.0f);
        return std::sin ((float) (juce::MathConstants<double>::twoPi * fmPhase[ix]) + phaseMod)
             * levelValue * envValue[ix] * keyGain * velocityGain;
    };

    const float feedback = fmFeedbackState * juce::jlimit (0.0f, 1.0f, params.fmFeedback) * 5.5f;
    op[5] = eval (5, feedback);

    float carrier = 0.0f;
    switch (juce::jlimit (0, 5, params.fmAlgorithm))
    {
        case 0:
            op[4] = eval (4, op[5] * 5.0f); op[3] = eval (3, op[4] * 5.0f); op[2] = eval (2, op[3] * 5.0f);
            op[1] = eval (1, op[2] * 5.0f); op[0] = eval (0, op[1] * 5.0f); carrier = op[0]; break;
        case 1:
            op[4] = eval (4, op[5] * 5.0f); op[3] = eval (3, op[4] * 5.0f); op[2] = eval (2, 0.0f);
            op[1] = eval (1, op[2] * 5.0f); op[0] = eval (0, op[1] * 5.0f); carrier = (op[3] + op[0]) * 0.7f; break;
        case 2:
            op[4] = eval (4, op[5] * 5.0f); op[3] = eval (3, 0.0f); op[2] = eval (2, op[3] * 5.0f);
            op[1] = eval (1, 0.0f); op[0] = eval (0, op[1] * 5.0f); carrier = (op[4] + op[2] + op[0]) * 0.52f; break;
        case 3:
            for (int i = 1; i < 5; ++i) op[(size_t) i] = eval (i, 0.0f);
            op[0] = eval (0, (op[1] + op[2] + op[3] + op[4] + op[5]) * 2.1f); carrier = op[0]; break;
        case 4:
            op[4] = eval (4, op[5] * 4.5f); op[3] = eval (3, op[4] * 4.5f); op[2] = eval (2, op[5] * 3.0f);
            op[1] = eval (1, op[2] * 4.0f); op[0] = eval (0, 0.0f); carrier = (op[3] + op[1] + op[0]) * 0.55f; break;
        default:
            for (int i = 0; i < 5; ++i) op[(size_t) i] = eval (i, 0.0f);
            for (auto value : op) carrier += value;
            carrier *= 0.28f; break;
    }

    fmFeedbackState = op[5];
    for (int i = 0; i < VoiceParameters::fmOperatorCount; ++i)
    {
        const auto ix = (size_t) i;
        const float frequency = params.fmOpFixedMode[ix] != 0
                              ? juce::jlimit (10.0f, 16000.0f, params.fmOpFixedHz[ix])
                              : fundamentalHz * juce::jlimit (0.125f, 16.0f, params.fmOpRatio[ix]);
        fmPhase[ix] += juce::jlimit (0.0, 0.49, frequency / sr);
        fmPhase[ix] -= std::floor (fmPhase[ix]);
    }
    return carrier;
}

void HybridVoice::renderSupersaw (float fundamentalHz, float detuneCents, float spread, float& left, float& right)
{
    static constexpr std::array<float, supersawVoiceCount> offsets {{ -1.0f, -0.66f, -0.33f, 0.0f, 0.33f, 0.66f, 1.0f }};
    left = right = 0.0f;
    const float width = juce::jlimit (0.0f, 1.0f, spread);
    const float cents = juce::jlimit (0.0f, 70.0f, detuneCents);

    for (int i = 0; i < supersawVoiceCount; ++i)
    {
        const float offset = offsets[(size_t) i];
        const float hz = fundamentalHz * std::pow (2.0f, offset * cents / 1200.0f);
        const double dt = juce::jlimit (0.0, 0.49, hz / sr);
        const float saw = wave (1, unisonPhase[(size_t) i], dt, 0.5f);
        const float pan = offset * width;
        const float lGain = std::sqrt (0.5f * (1.0f - pan));
        const float rGain = std::sqrt (0.5f * (1.0f + pan));
        left += saw * lGain;
        right += saw * rGain;
        unisonPhase[(size_t) i] += hz / sr;
        unisonPhase[(size_t) i] -= std::floor (unisonPhase[(size_t) i]);
    }
    left *= 0.20f;
    right *= 0.20f;
}

void HybridVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int count)
{
    if (! isVoiceActive()) return;
    jassert (count <= renderScratch.getNumSamples());
    if (count <= 0 || count > renderScratch.getNumSamples()) return;

    const int scratchChannels = juce::jmin (renderScratch.getNumChannels(), out.getNumChannels());
    renderScratch.clear (0, count);
    auto* scratchLeft = renderScratch.getWritePointer (0);
    auto* scratchRight = scratchChannels > 1 ? renderScratch.getWritePointer (1) : nullptr;

    for (int i = 0; i < count; ++i)
    {
        const float lfo = std::sin ((float) (juce::MathConstants<double>::twoPi * lfoPhase));
        const float env = ampEnv.getNextSample();

        float matrixPitch = 0.0f, matrixCutoff = 0.0f, matrixAmp = 0.0f;
        float matrixPulse = 0.0f, matrixFmAmount = 0.0f, matrixFmMix = 0.0f;
        float matrixWavetable = 0.0f, matrixWavefold = 0.0f;
        for (const auto& slot : params.modSlots)
        {
            if (slot.source == (int) ModSource::none || slot.destination == (int) ModDestination::none || std::abs (slot.amount) < 0.0001f)
                continue;
            const float mod = getModSourceValue (slot.source, lfo, env) * juce::jlimit (-1.0f, 1.0f, slot.amount);
            switch ((ModDestination) slot.destination)
            {
                case ModDestination::none:              break;
                case ModDestination::pitch:             matrixPitch += mod * 12.0f; break;
                case ModDestination::cutoff:            matrixCutoff += mod * 4.0f; break;
                case ModDestination::amplitude:         matrixAmp += mod; break;
                case ModDestination::pulseWidth:        matrixPulse += mod * 0.42f; break;
                case ModDestination::fmAmount:          matrixFmAmount += mod * 0.65f; break;
                case ModDestination::fmMix:             matrixFmMix += mod; break;
                case ModDestination::wavetablePosition: matrixWavetable += mod * 0.5f; break;
                case ModDestination::wavefold:          matrixWavefold += mod * 0.5f; break;
                default: break;
            }
        }

        const float globalSemis = params.masterTuneCents / 100.0f + pitchWheelSemitones + matrixPitch;
        const float f1 = baseHz * std::pow (2.0f, (globalSemis + params.lfoPitch * lfo) / 12.0f);
        const float f2 = baseHz * std::pow (2.0f, (globalSemis + params.osc2Semitones + params.osc2Detune / 100.0f) / 12.0f);
        const float fSub = f1 * 0.5f;
        const auto dt1 = juce::jlimit (0.0, 0.49, f1 / sr);
        const auto dt2 = juce::jlimit (0.0, 0.49, f2 / sr);
        const float dynamicPulseWidth = juce::jlimit (0.05f, 0.95f, params.pulseWidth + matrixPulse);
        const float dynamicFmAmount = juce::jlimit (0.0f, 1.2f, params.fmAmount + matrixFmAmount);
        const float dynamicFmMix = juce::jlimit (0.0f, 1.0f, params.fmMix + matrixFmMix);
        const float dynamicWavetablePosition = juce::jlimit (0.0f, 1.0f, params.wavetablePosition + matrixWavetable);
        const float dynamicWavefold = juce::jlimit (0.0f, 1.0f, params.wavefold + matrixWavefold);

        const float modulator = wave (0, phase2 * params.fmRatio, dt2 * params.fmRatio, 0.5f);
        const float phaseMod = modulator * dynamicFmAmount;
        const float o1 = wave (params.osc1Wave, phase1 + phaseMod, dt1, dynamicPulseWidth);
        const float o2 = wave (params.osc2Wave, phase2, dt2, dynamicPulseWidth);
        const float sub = wave (2, subPhase, juce::jlimit (0.0, 0.49, fSub / sr), 0.5f);
        const float ring = o1 * o2;
        const float noise = random.nextFloat() * 2.0f - 1.0f;
        const float fm6 = renderSixOperatorFm (f1);
        const float wt = wavetableWave (phase1, dynamicWavetablePosition, params.wavetableWarp);
        const float refWt = (params.referenceWavetable != nullptr && params.referenceWavetable->valid)
                          ? params.referenceWavetable->sample (phase1, dynamicWavetablePosition) : 0.0f;

        float uniL = 0.0f, uniR = 0.0f;
        if (params.supersawMix > 0.0001f)
            renderSupersaw (f1, params.unisonDetune, params.unisonSpread, uniL, uniR);

        float additive = 0.0f, additiveNorm = 0.0f;
        if (params.additiveMix > 0.0001f)
        {
            const float oddWeight = juce::jlimit (0.0f, 1.0f, params.oddEvenBalance) * 2.0f;
            const float evenWeight = (1.0f - juce::jlimit (0.0f, 1.0f, params.oddEvenBalance)) * 2.0f;
            for (int harmonic = 1; harmonic <= 12; ++harmonic)
            {
                if (f1 * harmonic >= sr * 0.47) break;
                const float parity = (harmonic % 2 == 1) ? oddWeight : evenWeight;
                const float amp = parity / std::pow ((float) harmonic, juce::jlimit (0.45f, 3.5f, params.harmonicTilt));
                additive += std::sin ((float) (juce::MathConstants<double>::twoPi * phase1 * harmonic)) * amp;
                additiveNorm += amp;
            }
            if (additiveNorm > 1.0e-6f) additive /= additiveNorm;
        }

        const float mono = o1 * params.osc1Mix + o2 * params.osc2Mix + sub * params.subMix + noise * params.noiseMix
                         + ring * params.ringMix + additive * params.additiveMix + fm6 * dynamicFmMix + wt * params.wavetableMix + refWt * params.referenceWavetableMix;
        scratchLeft[i] = (mono + uniL * params.supersawMix) * 0.21f;
        if (scratchRight != nullptr) scratchRight[i] = (mono + uniR * params.supersawMix) * 0.21f;

        cutoffScratch[(size_t) i] = juce::jlimit (20.0f, (float) (sr * 0.45),
                                                  params.cutoff * std::pow (2.0f, params.lfoCutoff * lfo + matrixCutoff));
        wavefoldScratch[(size_t) i] = dynamicWavefold;
        const float tremolo = 1.0f - params.lfoAmp * 0.5f + params.lfoAmp * 0.5f * (lfo + 1.0f);
        const float matrixGain = juce::jlimit (0.0f, 2.0f, 1.0f + matrixAmp);
        gainScratch[(size_t) i] = env * level * juce::jlimit (0.0f, 1.5f, tremolo) * matrixGain;

        phase1 += f1 / sr;
        phase2 += f2 / sr;
        subPhase += fSub / sr;
        lfoPhase += params.lfoRate / sr;
        phase1 -= std::floor (phase1);
        phase2 -= std::floor (phase2);
        subPhase -= std::floor (subPhase);
        lfoPhase -= std::floor (lfoPhase);
    }

    const int quality = qualityIndex (params.oversamplingQuality);
    if (quality == 0)
    {
        for (int ch = 0; ch < scratchChannels; ++ch)
        {
            auto* samples = renderScratch.getWritePointer (ch);
            for (int i = 0; i < count; ++i)
                samples[i] = foldSample (samples[i], wavefoldScratch[(size_t) i]);
        }
    }
    else
    {
        auto* oversampler = quality == 1 ? oversampling2x.get() : oversampling4x.get();
        jassert (oversampler != nullptr);
        juce::dsp::AudioBlock<float> fullBlock (renderScratch);
        auto baseBlock = fullBlock.getSubBlock (0, (size_t) count);
        auto upBlock = oversampler->processSamplesUp (baseBlock);
        const auto factor = (float) oversampler->getOversamplingFactor();

        auto interpolatedFold = [&] (size_t oversampledIndex)
        {
            const float basePosition = (float) oversampledIndex / factor;
            const int i0 = juce::jlimit (0, count - 1, (int) std::floor (basePosition));
            const int i1 = juce::jmin (count - 1, i0 + 1);
            const float fraction = juce::jlimit (0.0f, 1.0f, basePosition - (float) i0);
            return juce::jmap (fraction, wavefoldScratch[(size_t) i0], wavefoldScratch[(size_t) i1]);
        };

        for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
        {
            auto* samples = upBlock.getChannelPointer (ch);
            for (size_t i = 0; i < upBlock.getNumSamples(); ++i)
                samples[i] = foldSample (samples[i], interpolatedFold (i));
        }
        oversampler->processSamplesDown (baseBlock);
    }

    auto* outputLeft = out.getWritePointer (0);
    auto* outputRight = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    for (int i = 0; i < count; ++i)
    {
        filter.setCutoffFrequency (cutoffScratch[(size_t) i]);
        float sL = filter.processSample (0, scratchLeft[i]);
        float sR = scratchRight != nullptr ? filter.processSample (1, scratchRight[i]) : sL;
        const float gain = gainScratch[(size_t) i];
        sL *= gain;
        sR *= gain;
        outputLeft[start + i] += sL;
        if (outputRight != nullptr) outputRight[start + i] += sR;
    }

    if (! ampEnv.isActive()) clearCurrentNote();
}

SynthEngine::SynthEngine()
{
    for (int i = 0; i < 16; ++i) synth.addVoice (new HybridVoice());
    synth.addSound (new BasicSound());
}

void SynthEngine::prepare (double sr, int samplesPerBlock, int channels)
{
    sampleRate = sr;
    synth.setCurrentPlaybackSampleRate (sr);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<HybridVoice*> (synth.getVoice (i)))
            v->prepare (sr, samplesPerBlock, channels);

    const auto safeChannels = (juce::uint32) juce::jmax (1, channels);
    const auto safeBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    juce::dsp::ProcessSpec spec { sr, safeBlockSize, safeChannels };
    chorus.prepare (spec);
    delay.setMaximumDelayInSamples ((int) std::ceil (sr * 2.0));
    delay.prepare (spec);
    reverb.setSampleRate (sr);

    driveOversampling2x = std::make_unique<juce::dsp::Oversampling<float>> (
        safeChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    driveOversampling4x = std::make_unique<juce::dsp::Oversampling<float>> (
        safeChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    driveOversampling2x->initProcessing (safeBlockSize);
    driveOversampling4x->initProcessing (safeBlockSize);

    float voiceLatency2x = 0.0f, voiceLatency4x = 0.0f;
    if (synth.getNumVoices() > 0)
        if (auto* voice = dynamic_cast<HybridVoice*> (synth.getVoice (0)))
        {
            voiceLatency2x = voice->getOversamplingLatencySamples (1);
            voiceLatency4x = voice->getOversamplingLatencySamples (2);
        }

    intrinsicLatencySamples[0] = 0;
    intrinsicLatencySamples[1] = (int) std::lround (voiceLatency2x + driveOversampling2x->getLatencyInSamples());
    intrinsicLatencySamples[2] = (int) std::lround (voiceLatency4x + driveOversampling4x->getLatencyInSamples());
    fixedLatencySamples = juce::jmax (intrinsicLatencySamples[0], juce::jmax (intrinsicLatencySamples[1], intrinsicLatencySamples[2]));
    latencyCompensation.setMaximumDelayInSamples (juce::jmax (1, fixedLatencySamples + 8));
    latencyCompensation.prepare (spec);
    reset();
}

void SynthEngine::reset()
{
    chorus.reset();
    delay.reset();
    reverb.reset();
    if (driveOversampling2x != nullptr) driveOversampling2x->reset();
    if (driveOversampling4x != nullptr) driveOversampling4x->reset();
    latencyCompensation.reset();
}

void SynthEngine::setParameters (const VoiceParameters& p)
{
    current = p;
    current.oversamplingQuality = qualityIndex (current.oversamplingQuality);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<HybridVoice*> (synth.getVoice (i))) v->setParameters (current);

    chorus.setRate (juce::jlimit (0.02f, 10.0f, current.chorusRate));
    chorus.setDepth (juce::jlimit (0.0f, 1.0f, current.chorusDepth));
    chorus.setCentreDelay (7.0f);
    chorus.setFeedback (0.05f);
    chorus.setMix (juce::jlimit (0.0f, 1.0f, current.chorusMix));

    juce::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit (0.0f, 1.0f, current.reverbSize);
    rp.damping = juce::jlimit (0.0f, 1.0f, current.reverbDamping);
    rp.wetLevel = juce::jlimit (0.0f, 1.0f, current.reverbMix) * 0.55f;
    rp.dryLevel = 1.0f;
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
}

void SynthEngine::processEffects (juce::AudioBuffer<float>& audio)
{
    const auto n = audio.getNumSamples();
    const auto channels = audio.getNumChannels();
    const int quality = qualityIndex (current.oversamplingQuality);

    const auto processDrive = [&] (auto& block)
    {
        if (current.drive <= 0.0001f) return;
        const auto gain = 1.0f + current.drive * 10.0f;
        const auto norm = 1.0f / std::tanh (gain);
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* x = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
                x[i] = std::tanh (x[i] * gain) * norm;
        }
    };

    if (quality == 0)
    {
        juce::dsp::AudioBlock<float> block (audio);
        processDrive (block);
    }
    else
    {
        auto* oversampler = quality == 1 ? driveOversampling2x.get() : driveOversampling4x.get();
        jassert (oversampler != nullptr);
        juce::dsp::AudioBlock<float> block (audio);
        auto upBlock = oversampler->processSamplesUp (block);
        processDrive (upBlock);
        oversampler->processSamplesDown (block);
    }

    if (current.chorusMix > 0.0001f)
    {
        juce::dsp::AudioBlock<float> block (audio);
        juce::dsp::ProcessContextReplacing<float> context (block);
        chorus.process (context);
    }

    if (current.delayMix > 0.0001f || current.delayFeedback > 0.0001f)
    {
        const float delaySamples = juce::jlimit (1.0f, (float) (sampleRate * 1.9), current.delayTime * (float) sampleRate);
        const float wet = juce::jlimit (0.0f, 1.0f, current.delayMix);
        const float feedback = juce::jlimit (0.0f, 0.92f, current.delayFeedback);
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* x = audio.getWritePointer (ch);
                const float dry = x[i];
                const float d = delay.popSample (ch, delaySamples);
                delay.pushSample (ch, dry + d * feedback);
                x[i] = dry * (1.0f - wet * 0.35f) + d * wet;
            }
        }
    }

    if (current.reverbMix > 0.0001f)
    {
        if (channels > 1) reverb.processStereo (audio.getWritePointer (0), audio.getWritePointer (1), n);
        else reverb.processMono (audio.getWritePointer (0), n);
    }

    if (channels > 1 && std::abs (current.stereoWidth - 1.0f) > 0.001f)
    {
        auto* l = audio.getWritePointer (0);
        auto* r = audio.getWritePointer (1);
        const float width = juce::jlimit (0.0f, 2.0f, current.stereoWidth);
        for (int i = 0; i < n; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]) * width;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

    audio.applyGain (juce::Decibels::decibelsToGain (current.outputGainDb));
}

void SynthEngine::compensateLatency (juce::AudioBuffer<float>& audio)
{
    if (fixedLatencySamples <= 0) return;
    const int quality = qualityIndex (current.oversamplingQuality);
    const float delaySamples = (float) juce::jmax (0, fixedLatencySamples - intrinsicLatencySamples[(size_t) quality]);
    latencyCompensation.setDelay (delaySamples);

    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        auto* samples = audio.getWritePointer (ch);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            latencyCompensation.pushSample (ch, samples[i]);
            samples[i] = latencyCompensation.popSample (ch);
        }
    }
}

void SynthEngine::render (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    synth.renderNextBlock (audio, midi, 0, audio.getNumSamples());
    processEffects (audio);
    compensateLatency (audio);
}
