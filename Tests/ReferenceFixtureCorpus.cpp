#include "ReferenceFixtureCorpus.h"

#include <cmath>
#include <cstdint>

namespace ReferenceFixtureCorpus
{
namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;

float noiseSample (std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    const auto normalized = static_cast<float> ((state >> 8) & 0x00ffffffu) / 8388607.5f;
    return normalized - 1.0f;
}

float fadeInOut (float t, float duration, float attack, float release) noexcept
{
    const float in = juce::jlimit (0.0f, 1.0f, t / juce::jmax (0.0001f, attack));
    const float out = juce::jlimit (0.0f, 1.0f, (duration - t) / juce::jmax (0.0001f, release));
    return in * out;
}

float tonal (double t, float fundamental, float h2 = 0.0f, float h3 = 0.0f, float h4 = 0.0f) noexcept
{
    return std::sin ((float) (twoPi * fundamental * t))
         + h2 * std::sin ((float) (twoPi * fundamental * 2.0 * t))
         + h3 * std::sin ((float) (twoPi * fundamental * 3.0 * t))
         + h4 * std::sin ((float) (twoPi * fundamental * 4.0 * t));
}

constexpr std::array<Definition, 11> corpus {{
    { Kind::kick, "kick", 1.00f, { 42.0f, 90.0f, 0.05f, Lifecycle::oneShot, TransientCharacter::hard, SpectralBalance::dark, StereoCharacter::mono, false } },
    { Kind::snare, "snare-percussion", 0.90f, { 0.0f, 0.0f, 0.0f, Lifecycle::oneShot, TransientCharacter::hard, SpectralBalance::noisy, StereoCharacter::narrow, false } },
    { Kind::pluck, "short-pluck", 1.20f, { 216.0f, 224.0f, 0.35f, Lifecycle::plucked, TransientCharacter::hard, SpectralBalance::bright, StereoCharacter::mono, false } },
    { Kind::bass, "bass", 1.50f, { 108.0f, 112.0f, 0.55f, Lifecycle::sustained, TransientCharacter::medium, SpectralBalance::dark, StereoCharacter::mono, false } },
    { Kind::keys, "piano-keys", 2.00f, { 258.0f, 265.0f, 0.30f, Lifecycle::plucked, TransientCharacter::hard, SpectralBalance::balanced, StereoCharacter::narrow, false } },
    { Kind::bell, "mallet-bell", 2.20f, { 430.0f, 450.0f, 0.10f, Lifecycle::plucked, TransientCharacter::hard, SpectralBalance::bright, StereoCharacter::mono, false } },
    { Kind::lead, "sustained-lead", 1.50f, { 326.0f, 334.0f, 0.45f, Lifecycle::sustained, TransientCharacter::medium, SpectralBalance::bright, StereoCharacter::mono, false } },
    { Kind::pad, "pad", 2.50f, { 218.0f, 222.0f, 0.35f, Lifecycle::sustained, TransientCharacter::soft, SpectralBalance::balanced, StereoCharacter::wide, false } },
    { Kind::staccatoStrings, "staccato-strings", 0.90f, { 290.0f, 297.0f, 0.30f, Lifecycle::gated, TransientCharacter::hard, SpectralBalance::bright, StereoCharacter::narrow, false } },
    { Kind::noisyTexture, "noisy-texture", 1.60f, { 0.0f, 0.0f, 0.0f, Lifecycle::evolving, TransientCharacter::soft, SpectralBalance::noisy, StereoCharacter::wide, false } },
    { Kind::atmosphere, "evolving-atmosphere", 2.80f, { 108.0f, 112.0f, 0.20f, Lifecycle::evolving, TransientCharacter::soft, SpectralBalance::balanced, StereoCharacter::wide, false } }
}};
}

const std::array<Definition, 11>& definitions() noexcept
{
    return corpus;
}

juce::AudioBuffer<float> render (const Definition& fixture, double sampleRate)
{
    sampleRate = juce::jmax (8000.0, sampleRate);
    const int sampleCount = juce::jmax (1, (int) std::lround (fixture.durationSeconds * sampleRate));
    juce::AudioBuffer<float> audio (2, sampleCount);
    audio.clear();

    std::uint32_t noiseState = 0x524d5346u ^ static_cast<std::uint32_t> (fixture.kind);
    float smoothedNoise = 0.0f;
    double kickPhase = 0.0;

    for (int i = 0; i < sampleCount; ++i)
    {
        const float t = static_cast<float> (i / sampleRate);
        const float duration = fixture.durationSeconds;
        float left = 0.0f;
        float right = 0.0f;

        switch (fixture.kind)
        {
            case Kind::kick:
            {
                const float frequency = 46.0f + 72.0f * std::exp (-t * 18.0f);
                kickPhase += twoPi * frequency / sampleRate;
                const float body = std::sin ((float) kickPhase) * std::exp (-t * 6.8f);
                const float click = std::exp (-t * 70.0f) * std::sin ((float) (twoPi * 1700.0 * t));
                left = right = 0.62f * body + 0.10f * click;
                break;
            }
            case Kind::snare:
            {
                const float noise = noiseSample (noiseState);
                smoothedNoise += 0.35f * (noise - smoothedNoise);
                const float env = std::exp (-t * 8.5f);
                const float tone = tonal (t, 185.0f, 0.18f) * std::exp (-t * 11.0f);
                left = 0.38f * smoothedNoise * env + 0.16f * tone;
                right = 0.36f * noise * env + 0.16f * tone;
                break;
            }
            case Kind::pluck:
            {
                const float env = std::exp (-t * 4.7f) * juce::jlimit (0.0f, 1.0f, t / 0.003f);
                left = right = 0.36f * tonal (t, 220.0f, 0.48f, 0.25f, 0.12f) * env;
                break;
            }
            case Kind::bass:
            {
                const float env = fadeInOut (t, duration, 0.025f, 0.10f);
                left = right = 0.39f * tonal (t, 110.0f, 0.20f, 0.07f) * env;
                break;
            }
            case Kind::keys:
            {
                const float env1 = std::exp (-t * 1.45f) * juce::jlimit (0.0f, 1.0f, t / 0.004f);
                const float env2 = std::exp (-t * 2.1f);
                const float base = std::sin ((float) (twoPi * 261.6256 * t));
                const float upper = 0.42f * std::sin ((float) (twoPi * 523.2512 * t))
                                  + 0.20f * std::sin ((float) (twoPi * 784.8768 * t));
                left = 0.34f * (base * env1 + upper * env2);
                right = 0.33f * (base * env1 + 0.95f * upper * env2);
                break;
            }
            case Kind::bell:
            {
                const float env = std::exp (-t * 1.65f) * juce::jlimit (0.0f, 1.0f, t / 0.0025f);
                const float bell = std::sin ((float) (twoPi * 440.0 * t))
                                 + 0.46f * std::sin ((float) (twoPi * 1196.8 * t))
                                 + 0.27f * std::sin ((float) (twoPi * 1821.6 * t))
                                 + 0.15f * std::sin ((float) (twoPi * 2745.6 * t));
                left = right = 0.30f * bell * env;
                break;
            }
            case Kind::lead:
            {
                const float env = fadeInOut (t, duration, 0.018f, 0.08f);
                const float vibrato = 1.0f + 0.0018f * std::sin ((float) (twoPi * 5.1 * t));
                left = right = 0.30f * tonal (t * vibrato, 329.6276f, 0.40f, 0.22f, 0.12f) * env;
                break;
            }
            case Kind::pad:
            {
                const float env = fadeInOut (t, duration, 0.36f, 0.32f);
                const float slow = 0.78f + 0.22f * std::sin ((float) (twoPi * 0.23 * t));
                left = 0.27f * (tonal (t, 220.0f, 0.20f) + 0.25f * tonal (t, 219.2f)) * env * slow;
                right = 0.27f * (tonal (t, 220.0f, 0.20f) + 0.25f * tonal (t, 220.8f)) * env * (1.0f - 0.08f * std::sin ((float) (twoPi * 0.19 * t)));
                break;
            }
            case Kind::staccatoStrings:
            {
                const float gate = t < 0.42f ? fadeInOut (t, 0.46f, 0.010f, 0.045f) : 0.0f;
                const float base = tonal (t, 293.6648f, 0.52f, 0.29f, 0.16f);
                left = 0.27f * base * gate;
                right = 0.26f * (base + 0.08f * tonal (t, 294.6f, 0.35f)) * gate;
                break;
            }
            case Kind::noisyTexture:
            {
                const float noise = noiseSample (noiseState);
                smoothedNoise += 0.025f * (noise - smoothedNoise);
                const float env = fadeInOut (t, duration, 0.28f, 0.30f);
                const float motion = 0.45f + 0.30f * std::sin ((float) (twoPi * 0.37 * t));
                left = 0.34f * smoothedNoise * env * motion;
                right = 0.30f * noise * env * (0.65f - 0.22f * std::sin ((float) (twoPi * 0.29 * t)));
                break;
            }
            case Kind::atmosphere:
            {
                const float env = fadeInOut (t, duration, 0.45f, 0.40f);
                const float motionA = 0.72f + 0.20f * std::sin ((float) (twoPi * 0.17 * t));
                const float motionB = 0.70f + 0.22f * std::sin ((float) (twoPi * 0.13 * t + 0.8f));
                left = 0.24f * (tonal (t, 110.0f, 0.20f, 0.08f) + 0.24f * tonal (t, 165.0f)) * env * motionA;
                right = 0.24f * (tonal (t, 110.0f, 0.20f, 0.08f) + 0.24f * tonal (t, 164.5f)) * env * motionB;
                break;
            }
        }

        audio.setSample (0, i, juce::jlimit (-0.95f, 0.95f, left));
        audio.setSample (1, i, juce::jlimit (-0.95f, 0.95f, right));
    }

    return audio;
}
} // namespace ReferenceFixtureCorpus
