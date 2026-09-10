#pragma once

#include <JuceHeader.h>
#include <array>
#include <string_view>

namespace ReferenceFixtureCorpus
{
enum class Kind
{
    kick,
    snare,
    pluck,
    bass,
    keys,
    bell,
    lead,
    pad,
    staccatoStrings,
    noisyTexture,
    atmosphere
};

enum class Lifecycle
{
    oneShot,
    plucked,
    gated,
    sustained,
    evolving
};

enum class TransientCharacter
{
    soft,
    medium,
    hard
};

enum class SpectralBalance
{
    dark,
    balanced,
    bright,
    noisy
};

enum class StereoCharacter
{
    mono,
    narrow,
    wide
};

struct ExpectedFacts
{
    float minFundamentalHz = 0.0f;
    float maxFundamentalHz = 0.0f;
    float minPitchConfidence = 0.0f;
    Lifecycle lifecycle = Lifecycle::sustained;
    TransientCharacter transient = TransientCharacter::medium;
    SpectralBalance spectralBalance = SpectralBalance::balanced;
    StereoCharacter stereo = StereoCharacter::mono;
    bool nonlinearColourExpected = false;
};

struct Definition
{
    Kind kind;
    std::string_view id;
    float durationSeconds;
    ExpectedFacts expected;
};

const std::array<Definition, 11>& definitions() noexcept;
juce::AudioBuffer<float> render (const Definition& fixture, double sampleRate = 48000.0);
} // namespace ReferenceFixtureCorpus
