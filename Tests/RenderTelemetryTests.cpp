#include <JuceHeader.h>
#include "../Source/Matching/RenderTelemetry.h"
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
int fail (const char* message)
{
    std::cerr << "FAILED: " << message << '\n';
    return 1;
}

bool near (float actual, float expected, float tolerance = 1.0e-5f)
{
    return std::abs (actual - expected) <= tolerance;
}
}

int main()
{
    {
        juce::AudioBuffer<float> silence (2, 8);
        silence.clear();
        const auto telemetry = RenderTelemetry::analyze (silence);
        if (! telemetry.isFinite() || ! telemetry.isTechnicallySafe()
            || telemetry.totalSamples() != 16
            || telemetry.finiteSamples != 16 || telemetry.nonFiniteSamples != 0
            || telemetry.clippedSamples != 0 || telemetry.hasHardClipping()
            || ! near (telemetry.technicalSafetyScore(), 1.0f)
            || ! near (telemetry.peak, 0.0f) || ! near (telemetry.rms, 0.0f)
            || ! near (telemetry.dc, 0.0f) || ! near (telemetry.crestFactor, 0.0f))
            return fail ("silence telemetry is not neutral, finite and technically safe");
    }

    {
        juce::AudioBuffer<float> audio (1, 4);
        audio.setSample (0, 0, 0.0f);
        audio.setSample (0, 1, 0.5f);
        audio.setSample (0, 2, -0.5f);
        audio.setSample (0, 3, 1.25f);

        const auto telemetry = RenderTelemetry::analyze (audio);
        const float expectedRms = std::sqrt (2.0625f / 4.0f);
        if (! telemetry.isFinite() || telemetry.isTechnicallySafe()
            || telemetry.finiteSamples != 4 || telemetry.nonFiniteSamples != 0
            || telemetry.clippedSamples != 1 || ! telemetry.hasHardClipping()
            || ! near (telemetry.peak, 1.25f) || ! near (telemetry.rms, expectedRms)
            || ! near (telemetry.dc, 0.3125f)
            || ! near (telemetry.crestFactor, 1.25f / expectedRms)
            || ! near (telemetry.technicalSafetyScore(), 0.6f))
            return fail ("known finite fixture produced incorrect peak/RMS/DC/crest/clipping safety telemetry");

        const auto relaxed = RenderTelemetry::analyze (audio, 1.5f);
        if (relaxed.clippedSamples != 0 || relaxed.hasHardClipping()
            || ! relaxed.isTechnicallySafe() || ! near (relaxed.clipThreshold, 1.5f)
            || ! near (relaxed.technicalSafetyScore(), 1.0f))
            return fail ("custom clipping threshold was ignored by safety scoring");
    }

    {
        juce::AudioBuffer<float> invalid (1, 4);
        invalid.setSample (0, 0, 0.25f);
        invalid.setSample (0, 1, std::numeric_limits<float>::quiet_NaN());
        invalid.setSample (0, 2, std::numeric_limits<float>::infinity());
        invalid.setSample (0, 3, -0.25f);

        const auto telemetry = RenderTelemetry::analyze (invalid);
        if (telemetry.isFinite() || telemetry.isTechnicallySafe()
            || telemetry.totalSamples() != 4
            || telemetry.finiteSamples != 2 || telemetry.nonFiniteSamples != 2
            || telemetry.clippedSamples != 0
            || ! near (telemetry.technicalSafetyScore(), 0.0f)
            || ! near (telemetry.peak, 0.25f) || ! near (telemetry.rms, 0.25f)
            || ! near (telemetry.dc, 0.0f) || ! near (telemetry.crestFactor, 1.0f))
            return fail ("non-finite samples were not isolated and invalidated");
    }

    {
        juce::AudioBuffer<float> empty (1, 0);
        const auto telemetry = RenderTelemetry::analyze (empty);
        if (telemetry.isTechnicallySafe() || ! near (telemetry.technicalSafetyScore(), 0.0f))
            return fail ("empty render must not be considered a valid candidate");
    }

    std::cout << "Render telemetry tests passed.\n";
    return 0;
}
