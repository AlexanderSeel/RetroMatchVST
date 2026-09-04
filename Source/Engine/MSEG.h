#pragma once
#include <algorithm>
#include <array>
#include <cmath>

struct MsegParameters
{
    static constexpr int pointCount = 6;
    static constexpr int segmentCount = pointCount - 1;

    bool enabled = false;
    bool loopEnabled = false;
    int loopStartPoint = 1;
    int loopEndPoint = 3;

    std::array<float, pointCount> levels {{ 0.0f, 1.0f, 0.78f, 0.58f, 0.28f, 0.0f }};
    std::array<float, segmentCount> times {{ 0.025f, 0.090f, 0.180f, 0.320f, 0.420f }};
    std::array<float, segmentCount> curves {{ 0.15f, -0.10f, 0.0f, 0.10f, -0.15f }};
};

// Allocation-free, per-voice multi-segment envelope. A loop spans point
// loopStartPoint through loopEndPoint while the note is held. Releasing the note
// exits the loop without a discontinuity and continues toward the final point.
class MultiSegmentEnvelope
{
public:
    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = std::max (1.0, newSampleRate);
    }

    void setParameters (const MsegParameters& newParameters) noexcept
    {
        params = newParameters;
        params.loopStartPoint = std::clamp (params.loopStartPoint, 0, MsegParameters::pointCount - 2);
        params.loopEndPoint = std::clamp (params.loopEndPoint, params.loopStartPoint + 1, MsegParameters::pointCount - 1);
        for (auto& level : params.levels) level = std::clamp (level, 0.0f, 1.0f);
        for (auto& time : params.times) time = std::clamp (time, 0.001f, 12.0f);
        for (auto& curve : params.curves) curve = std::clamp (curve, -1.0f, 1.0f);
    }

    void noteOn() noexcept
    {
        released = false;
        active = true;
        currentLevel = params.levels[0];
        beginSegment (0, false);
    }

    void noteOff() noexcept
    {
        if (! active) return;
        released = true;

        if (params.loopEnabled
            && segmentIndex >= params.loopStartPoint
            && segmentIndex < params.loopEndPoint)
        {
            beginSegment (params.loopEndPoint, true);
        }
    }

    void reset() noexcept
    {
        active = false;
        released = false;
        segmentIndex = 0;
        sampleInSegment = 0;
        samplesInSegment = 1;
        segmentStartLevel = currentLevel = params.levels[0];
        segmentTargetLevel = params.levels[1];
    }

    float getNextSample() noexcept
    {
        if (! active) return currentLevel;

        const float progress = std::clamp ((float) sampleInSegment / (float) std::max (1, samplesInSegment), 0.0f, 1.0f);
        const float shaped = shapeProgress (progress, params.curves[(size_t) std::clamp (segmentIndex, 0, MsegParameters::segmentCount - 1)]);
        currentLevel = segmentStartLevel + (segmentTargetLevel - segmentStartLevel) * shaped;

        if (++sampleInSegment >= samplesInSegment)
        {
            currentLevel = segmentTargetLevel;
            advanceSegment();
        }

        return currentLevel;
    }

    float getCurrentValue() const noexcept { return currentLevel; }
    bool isActive() const noexcept { return active; }

private:
    MsegParameters params;
    double sampleRate = 44100.0;
    bool active = false;
    bool released = false;
    int segmentIndex = 0;
    int sampleInSegment = 0;
    int samplesInSegment = 1;
    float segmentStartLevel = 0.0f;
    float segmentTargetLevel = 1.0f;
    float currentLevel = 0.0f;

    static float shapeProgress (float t, float curve) noexcept
    {
        t = std::clamp (t, 0.0f, 1.0f);
        curve = std::clamp (curve, -1.0f, 1.0f);
        if (std::abs (curve) < 1.0e-4f) return t;

        const float exponent = 1.0f + std::abs (curve) * 5.0f;
        return curve > 0.0f ? std::pow (t, exponent)
                            : 1.0f - std::pow (1.0f - t, exponent);
    }

    void beginSegment (int newSegment, bool startFromCurrent) noexcept
    {
        segmentIndex = std::clamp (newSegment, 0, MsegParameters::segmentCount - 1);
        sampleInSegment = 0;
        samplesInSegment = std::max (1, (int) std::lround (params.times[(size_t) segmentIndex] * sampleRate));
        segmentStartLevel = startFromCurrent ? currentLevel : params.levels[(size_t) segmentIndex];
        segmentTargetLevel = params.levels[(size_t) segmentIndex + 1];
    }

    void advanceSegment() noexcept
    {
        const int reachedPoint = segmentIndex + 1;

        if (! released && params.loopEnabled && reachedPoint >= params.loopEndPoint)
        {
            currentLevel = params.levels[(size_t) params.loopStartPoint];
            beginSegment (params.loopStartPoint, false);
            return;
        }

        const int nextSegment = segmentIndex + 1;
        if (nextSegment >= MsegParameters::segmentCount)
        {
            active = false;
            currentLevel = params.levels.back();
            return;
        }

        beginSegment (nextSegment, false);
    }
};
