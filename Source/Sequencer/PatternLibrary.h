#pragma once

#include "StepSequencer.h"
#include <algorithm>
#include <array>

namespace RetroMatchSequencer
{
// Reusable musical starting points. These are intentionally independent of any
// synth preset: the caller owns the destination Core/state and can edit them.
struct PatternTemplate
{
    const char* name = "Empty";
    int length = 1;
    std::array<Step, maxSteps> steps {};
};

inline PatternTemplate makePatternTemplate (int index) noexcept
{
    PatternTemplate pattern;
    pattern.length = 8;
    for (auto& step : pattern.steps) { step = {}; step.rest = true; }

    auto set = [&] (int slot, int semitone, float velocity = 0.86f, float gate = 0.72f)
    {
        if (slot < 0 || slot >= maxSteps) return;
        auto& step = pattern.steps[(size_t) slot];
        step = {};
        step.semitone = semitone;
        step.velocity = velocity;
        step.gate = gate;
        step.probability = 1.0f;
        step.modulationProbability = 1.0f;
    };

    switch (std::clamp (index, 0, 7))
    {
        case 0:
            pattern.name = "Empty"; pattern.length = 1; return pattern;
        case 1:
            pattern.name = "Minor Pulse";
            for (int i : { 0, 3, 5, 7 }) set (i, i == 5 ? 3 : (i == 7 ? 7 : 0));
            break;
        case 2:
            pattern.name = "Major Arp"; pattern.length = 8;
            for (int i = 0; i < 8; ++i) set (i, std::array<int, 8> {{ 0, 4, 7, 12, 7, 4, 2, 4 }}[(size_t) i]);
            break;
        case 3:
            pattern.name = "Fifth Drive"; pattern.length = 16;
            for (int i = 0; i < 16; ++i) set (i, i % 4 == 0 ? 0 : (i % 4 == 2 ? 7 : 0), i % 4 == 0 ? 1.0f : 0.72f, 0.52f);
            break;
        case 4:
            pattern.name = "Trance Gate"; pattern.length = 16;
            for (int i = 0; i < 16; ++i) set (i, std::array<int, 4> {{ 0, 7, 12, 7 }}[(size_t) (i / 4)], 0.78f, 0.48f);
            break;
        case 5:
            pattern.name = "Broken Bells"; pattern.length = 12;
            for (int i : { 0, 2, 5, 7, 9, 11 }) set (i, std::array<int, 6> {{ 0, 7, 4, 12, 7, 2 }}[(size_t) (i / 2)], 0.74f, 0.38f);
            break;
        case 6:
            pattern.name = "Odd Steps"; pattern.length = 7;
            for (int i = 0; i < pattern.length; ++i) set (i, std::array<int, 7> {{ 0, 2, 5, 7, 9, 7, 5 }}[(size_t) i], 0.82f, 0.64f);
            break;
        case 7:
            pattern.name = "Percussive Rest"; pattern.length = 16;
            for (int i : { 0, 3, 6, 8, 10, 14 }) set (i, i % 8 == 0 ? 0 : 7, 0.9f, 0.24f);
            break;
    }
    return pattern;
}

inline constexpr int patternTemplateCount = 8;
}
