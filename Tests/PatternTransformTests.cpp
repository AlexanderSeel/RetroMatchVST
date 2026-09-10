#include "../Source/Sequencer/PatternTransforms.h"

#include <array>
#include <cmath>
#include <iostream>

using namespace RetroMatchSequencer;

namespace
{
int fail (const char* message)
{
    std::cerr << "Pattern transform test failed: " << message << '\n';
    return 1;
}

bool sameStep (const Step& a, const Step& b)
{
    return a.enabled == b.enabled && a.rest == b.rest && a.tie == b.tie
        && a.semitone == b.semitone && a.octave == b.octave
        && std::abs (a.velocity - b.velocity) < 1.0e-6f
        && std::abs (a.gate - b.gate) < 1.0e-6f
        && std::abs (a.probability - b.probability) < 1.0e-6f
        && std::abs (a.modulationProbability - b.modulationProbability) < 1.0e-6f
        && a.ratchet == b.ratchet
        && std::abs (a.microTiming - b.microTiming) < 1.0e-6f
        && a.glide == b.glide && a.macro == b.macro;
}
}

int main()
{
    {
        Step source;
        source.semitone = 7;
        source.octave = -2;
        source.velocity = 0.73f;
        source.gate = 0.61f;
        source.probability = 0.82f;
        source.ratchet = 3;
        source.microTiming = -0.12f;
        source.glide = true;
        source.macro = {{ 0.22f, 0.88f }};
        const auto inverted = PatternTransforms::inverted (source);
        if (inverted.semitone != -7 || inverted.octave != 2
            || inverted.velocity != source.velocity || inverted.gate != source.gate
            || inverted.probability != source.probability || inverted.ratchet != source.ratchet
            || inverted.microTiming != source.microTiming || inverted.glide != source.glide
            || inverted.macro != source.macro)
            return fail ("pitch inversion changed non-pitch step semantics");
    }

    {
        std::array<Step, maxSteps> pattern {};
        pattern[0].semitone = -48;
        pattern[0].octave = -4;
        pattern[1].semitone = 12;
        pattern[1].octave = 1;
        PatternTransforms::invert (pattern, 2);
        if (pattern[0].semitone != 48 || pattern[0].octave != 4
            || pattern[1].semitone != -12 || pattern[1].octave != -1)
            return fail ("pattern inversion is not bounded around the root");
    }

    {
        std::array<Step, maxSteps> pattern {};
        pattern[0].semitone = 1;
        pattern[1].semitone = 5;
        pattern[2].semitone = 9;
        pattern[1].velocity = 0.63f;
        pattern[1].macro = {{ 0.14f, 0.76f }};
        const auto copied = pattern[1];
        const int length = PatternTransforms::duplicateAfter (pattern, 3, 1);
        if (length != 4 || ! sameStep (pattern[2], copied) || pattern[3].semitone != 9)
            return fail ("duplicate did not insert an exact step copy and shift later steps");
    }

    {
        std::array<Step, maxSteps> first {}, second {};
        for (int i = 0; i < 8; ++i)
        {
            first[(size_t) i].velocity = second[(size_t) i].velocity = 0.5f + i * 0.04f;
            first[(size_t) i].gate = second[(size_t) i].gate = 0.55f;
            first[(size_t) i].microTiming = second[(size_t) i].microTiming = 0.0f;
        }
        PatternTransforms::humanize (first, 8, 0x1234abcdU);
        PatternTransforms::humanize (second, 8, 0x1234abcdU);
        for (int i = 0; i < 8; ++i)
        {
            if (! sameStep (first[(size_t) i], second[(size_t) i]))
                return fail ("humanize is not deterministic for a fixed seed");
            const auto& step = first[(size_t) i];
            if (step.velocity < 0.0f || step.velocity > 1.0f
                || step.gate < 0.02f || step.gate > 1.0f
                || step.microTiming < -0.45f || step.microTiming > 0.45f)
                return fail ("humanize escaped real-time-safe step bounds");
        }
    }

    {
        std::array<Step, maxSteps> pattern {};
        pattern[0].rest = true;
        pattern[0].velocity = 0.42f;
        pattern[1].enabled = false;
        pattern[1].velocity = 0.37f;
        PatternTransforms::humanize (pattern, 2, 42U);
        if (pattern[0].velocity != 0.42f || pattern[1].velocity != 0.37f)
            return fail ("humanize changed rest/disabled steps");
    }

    std::cout << "Sequencer pattern transform tests passed.\n";
    return 0;
}
