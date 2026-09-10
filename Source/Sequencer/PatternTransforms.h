#pragma once

#include "StepSequencer.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace RetroMatchSequencer::PatternTransforms
{
struct HumanizeAmount
{
    float velocity = 0.06f;
    float gate = 0.06f;
    float microTiming = 0.05f;
};

inline std::uint32_t nextRandom (std::uint32_t& state) noexcept
{
    if (state == 0) state = 0x9e3779b9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

inline float bipolarRandom (std::uint32_t& state) noexcept
{
    return (float) (nextRandom (state) & 0x00ffffffu) / (float) 0x007fffffu - 1.0f;
}

inline Step inverted (Step step) noexcept
{
    step.semitone = std::clamp (-step.semitone, -48, 48);
    step.octave = std::clamp (-step.octave, -4, 4);
    return step;
}

inline Step humanized (Step step, std::uint32_t& state, HumanizeAmount amount = {}) noexcept
{
    amount.velocity = std::clamp (amount.velocity, 0.0f, 0.25f);
    amount.gate = std::clamp (amount.gate, 0.0f, 0.25f);
    amount.microTiming = std::clamp (amount.microTiming, 0.0f, 0.20f);
    step.velocity = std::clamp (step.velocity + bipolarRandom (state) * amount.velocity, 0.0f, 1.0f);
    step.gate = std::clamp (step.gate + bipolarRandom (state) * amount.gate, 0.02f, 1.0f);
    step.microTiming = std::clamp (step.microTiming + bipolarRandom (state) * amount.microTiming, -0.45f, 0.45f);
    return step;
}

inline void invert (std::array<Step, maxSteps>& steps, int length) noexcept
{
    length = std::clamp (length, 0, maxSteps);
    for (int i = 0; i < length; ++i) steps[(size_t) i] = inverted (steps[(size_t) i]);
}

inline void humanize (std::array<Step, maxSteps>& steps, int length, std::uint32_t seed,
                      HumanizeAmount amount = {}) noexcept
{
    length = std::clamp (length, 0, maxSteps);
    for (int i = 0; i < length; ++i)
    {
        auto& step = steps[(size_t) i];
        if (! step.enabled || step.rest) continue;
        step = humanized (step, seed, amount);
    }
}

// Inserts a copy immediately after sourceIndex. Later steps shift right and the
// final step is discarded only when the pattern is already at maxSteps.
inline int duplicateAfter (std::array<Step, maxSteps>& steps, int length, int sourceIndex) noexcept
{
    length = std::clamp (length, 1, maxSteps);
    sourceIndex = std::clamp (sourceIndex, 0, length - 1);
    const int destination = std::min (sourceIndex + 1, maxSteps - 1);
    const auto copy = steps[(size_t) sourceIndex];
    const int newLength = std::min (maxSteps, length + 1);
    for (int i = newLength - 1; i > destination; --i)
        steps[(size_t) i] = steps[(size_t) (i - 1)];
    steps[(size_t) destination] = copy;
    return newLength;
}
}
