#pragma once
#include <JuceHeader.h>
#include <array>

// Slot IDs stay fixed for automation. Types may repeat; order is the slot order
// within each stage. Adding a processor requires a descriptor and a DSP case.
struct FxModuleParameters
{
    static constexpr int slotCount = 8;
    int type = 0, stage = 0;
    bool bypass = false;
    float amount = 0.5f, rate = 0.25f, feedback = 0.25f, mix = 0.5f;
};

struct FxModuleDescriptor { const char* name; const char* amount; const char* rate; const char* feedback; };
inline constexpr std::array<FxModuleDescriptor, 14> fxModuleCatalog {{
    { "Empty", "Amount", "Rate", "Feedback" },
    { "Low-pass filter", "Cutoff", "LFO rate", "Resonance" },
    { "High-pass filter", "Cutoff", "LFO rate", "Resonance" },
    { "Saturation", "Drive", "Tone", "Bias" },
    { "Hard clip", "Drive", "Tone", "Bias" },
    { "Wavefolder", "Fold", "Tone", "Bias" },
    { "Chorus", "Depth", "Rate", "Feedback" },
    { "Flanger", "Depth", "Rate", "Feedback" },
    { "Delay", "Time", "Tone", "Feedback" },
    { "Reverb", "Room", "Damping", "Width" },
    { "Bit crusher", "Bits", "Sample hold", "Tone" },
    { "Tremolo", "Depth", "Rate", "Shape" },
    { "Ring modulator", "Depth", "Frequency", "Stereo phase" },
    { "Compressor", "Threshold", "Attack", "Ratio" }
}};

class ModuleRack
{
public:
    void prepare (double sr, int blockSize, int channels)
    {
        sampleRate = sr;
        const juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, blockSize), (juce::uint32) juce::jmax (1, channels) };
        for (auto& m : modules)
        {
            m.filter.prepare (spec); m.chorus.prepare (spec); m.compressor.prepare (spec);
            m.delay.setMaximumDelayInSamples ((int) (sr * 2.1)); m.delay.prepare (spec);
            m.reverb.setSampleRate (sr); m.dry.setSize (channels, blockSize);
            m.reset();
        }
    }
    void reset() { for (auto& m : modules) m.reset(); }
    void process (juce::AudioBuffer<float>& audio, const std::array<FxModuleParameters, FxModuleParameters::slotCount>& parameters, int stage)
    {
        for (size_t index = 0; index < modules.size(); ++index)
        {
            const auto& p = parameters[index]; auto& m = modules[index];
            const int type = juce::jlimit (0, (int) fxModuleCatalog.size() - 1, p.type);
            if (m.type != type || m.stage != p.stage || m.bypassed != p.bypass)
            { m.reset(); m.type = type; m.stage = p.stage; m.bypassed = p.bypass; }
            if (type == 0 || p.bypass || p.stage != stage) continue;
            const float a = juce::jlimit (0.0f, 1.0f, p.amount), r = juce::jlimit (0.0f, 1.0f, p.rate);
            const float f = juce::jlimit (0.0f, 1.0f, p.feedback), mix = juce::jlimit (0.0f, 1.0f, p.mix);
            m.dry.makeCopyOf (audio, true);
            juce::dsp::AudioBlock<float> block (audio); juce::dsp::ProcessContextReplacing<float> context (block);
            if (type == 1 || type == 2)
            {
                m.filter.setType (type == 1 ? juce::dsp::StateVariableTPTFilterType::lowpass : juce::dsp::StateVariableTPTFilterType::highpass);
                m.filter.setResonance (0.55f + f * 9.0f);
                const double hz = 0.05 * std::pow (400.0, r);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                {
                    const float cutoff = 30.0f * std::pow (600.0f, a) * std::pow (2.0f, 0.5f * (float) std::sin (m.phase));
                    m.filter.setCutoffFrequency (juce::jlimit (20.0f, (float) sampleRate * 0.45f, cutoff));
                    for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.setSample (ch, i, m.filter.processSample (ch, audio.getSample (ch, i)));
                    m.phase = std::fmod (m.phase + juce::MathConstants<double>::twoPi * hz / sampleRate, juce::MathConstants<double>::twoPi);
                }
            }
            else if (type == 6 || type == 7)
            {
                m.chorus.setDepth (a); m.chorus.setRate (0.03f + r * 8.0f); m.chorus.setCentreDelay (type == 6 ? 12.0f : 1.5f);
                m.chorus.setFeedback (f * 0.85f); m.chorus.setMix (1); m.chorus.process (context);
            }
            else if (type == 9)
            {
                juce::Reverb::Parameters reverb;
                reverb.roomSize = a; reverb.damping = r; reverb.width = f; reverb.wetLevel = 1; reverb.dryLevel = 0;
                m.reverb.setParameters (reverb);
                if (audio.getNumChannels() > 1) m.reverb.processStereo (audio.getWritePointer (0), audio.getWritePointer (1), audio.getNumSamples());
                else m.reverb.processMono (audio.getWritePointer (0), audio.getNumSamples());
            }
            else if (type == 13)
            {
                m.compressor.setThreshold (-48.0f * a); m.compressor.setRatio (1.0f + f * 19.0f);
                m.compressor.setAttack (0.1f + r * 99.9f); m.compressor.setRelease (100); m.compressor.process (context);
            }
            else
            {
                const float tone = 0.01f + r * 0.98f;
                const int holdSamples = 1 + (int) (r * 63);
                const float steps = std::pow (2.0f, 2.0f + a * 14.0f);
                const double hz = type == 12 ? 10.0 * std::pow (200.0, r) : 0.05 * std::pow (400.0, r);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                {
                    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    {
                        const auto c = (size_t) juce::jlimit (0, 1, ch); const float dry = audio.getSample (ch, i); float wet = dry;
                        if (type >= 3 && type <= 5)
                        {
                            const float bias = (f - 0.5f) * 0.4f, gain = 1.0f + a * 15.0f;
                            auto shape = [type] (float x) { return type == 3 ? std::tanh (x) : type == 4 ? juce::jlimit (-1.0f, 1.0f, x) : std::sin (x); };
                            wet = shape ((dry + bias) * gain) - shape (bias * gain);
                            m.tone[c] += tone * (wet - m.tone[c]); wet = m.tone[c];
                        }
                        if (type == 8)
                        {
                            wet = m.delay.popSample (ch, (float) sampleRate * (0.01f + a * 1.99f));
                            m.tone[c] += tone * (wet - m.tone[c]); wet = m.tone[c];
                            m.delay.pushSample (ch, dry + wet * f * 0.90f);
                        }
                        if (type == 10)
                        {
                            if (m.holdCounter == 0) m.held[c] = std::round (dry * steps) / steps;
                            m.tone[c] += (0.01f + f * 0.98f) * (m.held[c] - m.tone[c]); wet = m.tone[c];
                        }
                        if (type == 11)
                        {
                            const float lfo = f < 0.5f ? (float) std::sin (m.phase) : (std::sin (m.phase) >= 0 ? 1.0f : -1.0f);
                            wet = dry * (1.0f - a * (0.5f + 0.5f * lfo));
                        }
                        if (type == 12) wet = dry * ((1.0f - a) + a * (float) std::sin (m.phase + ch * f * juce::MathConstants<double>::pi));
                        audio.setSample (ch, i, wet);
                    }
                    m.holdCounter = (m.holdCounter + 1) % holdSamples;
                    m.phase = std::fmod (m.phase + juce::MathConstants<double>::twoPi * hz / sampleRate, juce::MathConstants<double>::twoPi);
                }
            }
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    audio.setSample (ch, i, juce::jmap (mix, m.dry.getSample (ch, i), audio.getSample (ch, i)));
        }
    }
private:
    struct Module
    {
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::dsp::Chorus<float> chorus;
        juce::dsp::DelayLine<float> delay;
        juce::dsp::Compressor<float> compressor;
        juce::Reverb reverb;
        juce::AudioBuffer<float> dry;
        std::array<float, 2> tone {}, held {};
        double phase = 0; int holdCounter = 0, type = 0, stage = 0; bool bypassed = false;
        void reset() { filter.reset(); chorus.reset(); delay.reset(); compressor.reset(); reverb.reset(); tone.fill (0); held.fill (0); phase = 0; holdCounter = 0; }
    };
    std::array<Module, FxModuleParameters::slotCount> modules;
    double sampleRate = 44100;
};
