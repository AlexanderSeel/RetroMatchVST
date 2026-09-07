#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace TempoSync
{
inline constexpr std::array<const char*, 8> divisionNames {{
    "1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1"
}};

// Length in quarter-note beats. A quarter note is one beat at the host/manual BPM.
inline constexpr std::array<float, 8> quarterNoteBeats {{
    0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f
}};

inline juce::StringArray divisionLabels()
{
    juce::StringArray labels;
    for (const auto* name : divisionNames) labels.add (name);
    return labels;
}

inline int clampDivision (int division) noexcept
{
    return juce::jlimit (0, (int) divisionNames.size() - 1, division);
}

inline float clampBpm (float bpm) noexcept
{
    return juce::jlimit (40.0f, 300.0f, std::isfinite (bpm) ? bpm : 120.0f);
}

inline float seconds (int division, float bpm) noexcept
{
    const auto beats = quarterNoteBeats[(size_t) clampDivision (division)];
    return beats * 60.0f / clampBpm (bpm);
}

inline float frequencyHz (int division, float bpm) noexcept
{
    return 1.0f / juce::jmax (0.0001f, seconds (division, bpm));
}
}
