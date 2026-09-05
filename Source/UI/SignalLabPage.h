#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"

class SignalLabPage final : public juce::Component, private juce::Timer
{
public:
    explicit SignalLabPage (RetroMatchSynthAudioProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719));
        auto area = getLocalBounds().toFloat().reduced (16);
        const auto led = findColour (RetroLookAndFeel::primaryLed), accent = findColour (RetroLookAndFeel::secondaryLed);
        g.setColour (led); g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawText ("SIGNAL LAB  /  LIVE OUTPUT + SYNTHESIS", area.removeFromTop (32), juce::Justification::centredLeft);
        auto displays = area.removeFromTop (area.getHeight() * 0.48f);
        const float width = displays.getWidth() / 3.0f;
        auto scope = screen (g, displays.removeFromLeft (width).reduced (3), "01 / OUTPUT L + R", led);
        auto spectrum = screen (g, displays.removeFromLeft (width).reduced (3), "02 / SPECTRUM  -80..0 dBFS", accent);
        auto stereo = screen (g, displays.reduced (3), "03 / STEREO FIELD", led);
        int trigger = 0;
        for (int i = 1; i < 1024; ++i)
        {
            const int at = (writeIndex + i) % size, before = (at + size - 1) % size;
            if (left[(size_t) before] <= 0.0f && left[(size_t) at] > 0.0f) { trigger = i; break; }
        }
        for (int channel = 0; channel < 2; ++channel)
        {
            juce::Path wave;
            for (int i = 0; i < 512; ++i)
            {
                const int at = (writeIndex + trigger + i * 2) % size;
                const float value = channel == 0 ? left[(size_t) at] : right[(size_t) at];
                const float x = scope.getX() + i / 511.0f * scope.getWidth();
                const float y = scope.getCentreY() - juce::jlimit (-1.0f, 1.0f, value) * scope.getHeight() * 0.46f;
                if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
            }
            glow (g, wave, channel == 0 ? led : accent, 1.2f);
        }
        for (int band = 0; band < 48; ++band)
        {
            const float x = spectrum.getX() + band * spectrum.getWidth() / 48.0f;
            const float h = bands[(size_t) band] * spectrum.getHeight();
            auto bar = juce::Rectangle<float> (x, spectrum.getBottom() - h, std::max (1.0f, spectrum.getWidth() / 48 - 1), h);
            g.setColour (accent.withAlpha (0.12f)); g.fillRect (bar.expanded (1.5f));
            g.setGradientFill (juce::ColourGradient (accent, bar.getTopLeft(), led.withAlpha (0.2f), bar.getBottomLeft(), false)); g.fillRect (bar);
            g.setColour (juce::Colour (0xff03090c).withAlpha (0.6f));
            for (float y = spectrum.getBottom(); y > bar.getY(); y -= 5) g.drawHorizontalLine ((int) y, bar.getX(), bar.getRight());
        }
        juce::Path field;
        for (int i = 0; i < size; i += 4)
        {
            const float mid = (left[(size_t) i] + right[(size_t) i]) * 0.5f;
            const float side = (left[(size_t) i] - right[(size_t) i]) * 0.5f;
            const float x = stereo.getCentreX() + juce::jlimit (-1.0f, 1.0f, side) * stereo.getWidth() * 0.48f;
            const float y = stereo.getCentreY() - juce::jlimit (-1.0f, 1.0f, mid) * stereo.getHeight() * 0.48f;
            if (i == 0) field.startNewSubPath (x, y); else field.lineTo (x, y);
        }
        glow (g, field, led, 0.8f);
        area.removeFromTop (12);
        auto route = screen (g, area, "PATCH ROUTING / MIX LEVELS  /  REAL OUTPUT ABOVE", led);
        auto params = proc.getCurrentVoiceParameters();
        const std::array<float, 6> levels { params.osc1Mix, params.osc2Mix, params.fmMix,
            params.wavetableMix + params.referenceWavetableMix + params.userWavetableMix,
            params.additiveMix + params.subMix, params.noiseMix + params.supersawMix };
        const juce::String names[] { "OSC 1", "OSC 2", "6-OP FM", "WAVETABLES", "HARMONICS", "NOISE / UNISON" };
        const float nodeWidth = route.getWidth() * 0.27f, rowHeight = route.getHeight() / 6.0f;
        const float busX = route.getX() + route.getWidth() * 0.38f;
        for (int i = 0; i < 6; ++i)
        {
            auto node = juce::Rectangle<float> (route.getX(), route.getY() + i * rowHeight, nodeWidth, rowHeight - 4);
            g.setColour (juce::Colour (0xff182529)); g.fillRoundedRectangle (node, 4);
            const auto colour = levels[(size_t) i] > 0.001f ? led : juce::Colour (0xff42555b);
            g.setColour (colour.withAlpha (0.18f)); g.drawRoundedRectangle (node, 4, 1);
            g.setColour (colour); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText (names[i] + "  " + juce::String (levels[(size_t) i], 2), node.reduced (6, 0), juce::Justification::centredLeft);
            juce::Path wire; wire.startNewSubPath (node.getRight(), node.getCentreY());
            wire.cubicTo (busX, node.getCentreY(), busX, route.getCentreY(), busX + 12, route.getCentreY());
            glow (g, wire, colour.withAlpha (levels[(size_t) i] > 0.001f ? 0.7f : 0.15f), 1.0f);
        }
        const juce::String stages[] { "FILTER", "DRIVE", "SPACE", "OUT" };
        const juce::String values[] { juce::String ((int) params.cutoff) + " Hz", juce::String (params.drive * 100, 0) + "%",
            juce::String (params.reverbMix * 100, 0) + "%", juce::String (params.outputGainDb, 1) + " dB" };
        const float cell = (route.getRight() - busX - 12) / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto node = juce::Rectangle<float> (busX + 12 + i * cell, route.getCentreY() - 30, cell - 6, 60);
            g.setColour (juce::Colour (0xff203035)); g.fillRoundedRectangle (node.translated (0, 3), 4);
            g.setColour (juce::Colour (0xff0e191e)); g.fillRoundedRectangle (node, 4);
            g.setColour (accent.withAlpha (0.5f)); g.drawRoundedRectangle (node, 4, 1);
            g.setColour (accent); g.drawText (stages[i], node.withHeight (30), juce::Justification::centred);
            g.setColour (led); g.drawText (values[i], node.withTrimmedTop (30), juce::Justification::centred);
            if (i < 3) { g.setColour (led); g.drawLine (node.getRight(), node.getCentreY(), node.getRight() + 6, node.getCentreY(), 1.5f); }
        }
    }
private:
    RetroMatchSynthAudioProcessor& proc;
    static constexpr int size = 2048;
    std::array<float, size> left {}, right {};
    std::array<float, AudioVisualBuffer::capacity> incomingLeft {}, incomingRight {};
    std::array<float, size * 2> fftData {};
    std::array<float, 48> bands {};
    int writeIndex = 0;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { size, juce::dsp::WindowingFunction<float>::hann, true };
    static void glow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width)
    {
        g.setColour (colour.withMultipliedAlpha (0.07f)); g.strokePath (path, juce::PathStrokeType (width + 7));
        g.setColour (colour.withMultipliedAlpha (0.18f)); g.strokePath (path, juce::PathStrokeType (width + 3));
        g.setColour (colour); g.strokePath (path, juce::PathStrokeType (width));
    }
    static juce::Rectangle<float> screen (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, juce::Colour led)
    {
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (bounds.translated (0, 3), 7);
        g.setColour (juce::Colour (0xff516166)); g.drawRoundedRectangle (bounds, 7, 1);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff020709), bounds.getTopLeft(), juce::Colour (0xff0c1b20), bounds.getBottomRight(), false));
        g.fillRoundedRectangle (bounds.reduced (1), 7);
        auto area = bounds.reduced (9); g.setColour (led); g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (title, area.removeFromTop (22), juce::Justification::centredLeft);
        for (int i = 1; i < 5; ++i)
        {
            g.setColour (led.withAlpha (0.06f));
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * i / 5), area.getY(), area.getBottom());
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * i / 5), area.getX(), area.getRight());
        }
        return area;
    }
    void timerCallback() override
    {
        const int n = proc.visualAudio.read (incomingLeft.data(), incomingRight.data(), (int) incomingLeft.size());
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) writeIndex] = incomingLeft[(size_t) i]; right[(size_t) writeIndex] = incomingRight[(size_t) i];
            writeIndex = (writeIndex + 1) % size;
        }
        if (n == 0) { for (auto& value : bands) value *= 0.85f; left.fill (0); right.fill (0); }
        if (isVisible() && n > 0)
        {
            fftData.fill (0);
            for (int i = 0; i < size; ++i) fftData[(size_t) i] = (left[(size_t) ((writeIndex + i) % size)] + right[(size_t) ((writeIndex + i) % size)]) * 0.5f;
            window.multiplyWithWindowingTable (fftData.data(), size); fft.performFrequencyOnlyForwardTransform (fftData.data());
            const double sr = std::max (8000.0, proc.getSampleRate());
            for (int b = 0; b < 48; ++b)
            {
                const int lo = juce::jlimit (1, size / 2 - 1, (int) (20.0 * std::pow (1000.0, b / 48.0) * size / sr));
                const int hi = juce::jlimit (lo + 1, size / 2, (int) (20.0 * std::pow (1000.0, (b + 1) / 48.0) * size / sr));
                float magnitude = 0;
                for (int k = lo; k < hi; ++k) magnitude = std::max (magnitude, fftData[(size_t) k] * 2.0f / size);
                const float value = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (magnitude, -80.0f) + 80) / 80);
                bands[(size_t) b] = std::max (value, bands[(size_t) b] * 0.87f);
            }
        }
        if (isVisible()) repaint();
    }
};
