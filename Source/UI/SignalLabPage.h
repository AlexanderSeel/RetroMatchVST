#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include <vector>

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
        g.drawText ("SIGNAL LAB  /  LIVE OUTPUT + WHOLE-SYNTH PATCH MAP", area.removeFromTop (32), juce::Justification::centredLeft);

        auto displays = area.removeFromTop (area.getHeight() * 0.38f);
        const float width = displays.getWidth() / 3.0f;
        auto scope = screen (g, displays.removeFromLeft (width).reduced (3), "01 / OUTPUT L + R", led);
        auto spectrum = screen (g, displays.removeFromLeft (width).reduced (3), "02 / SPECTRUM  -80..0 dBFS", accent);
        auto stereo = screen (g, displays.reduced (3), "03 / STEREO FIELD", led);
        drawScope (g, scope, led, accent);
        drawSpectrum (g, spectrum, led, accent);
        drawStereo (g, stereo, led);

        area.removeFromTop (10);
        graphViewport = area;
        drawPatchMap (g, area, led, accent);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! graphViewport.contains (e.position)) return;
        selectedNode = hitTestNode (e.position);
        if (selectedNode < 0 || e.mods.isMiddleButtonDown() || e.mods.isRightButtonDown())
        {
            panning = true;
            panDragStart = e.position;
            panAtDragStart = graphPan;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! panning) return;
        graphPan = panAtDragStart + (e.position - panDragStart);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { panning = false; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! graphViewport.contains (e.position)) return;
        const int hit = hitTestNode (e.position);
        if (hit >= 0 && juce::isPositiveAndBelow (hit, (int) nodes.size()))
        {
            selectedNode = hit;
            navigateTo (nodes[(size_t) hit]);
        }
        else
        {
            graphZoom = 0.82f;
            graphPan = { 18.0f, 18.0f };
            repaint();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! graphViewport.contains (e.position)) return;
        const auto before = toWorld (e.position);
        graphZoom = juce::jlimit (0.35f, 2.2f, graphZoom * (1.0f + wheel.deltaY * 0.18f));
        graphPan = e.position - graphViewport.getPosition() - before * graphZoom;
        repaint();
    }

private:
    struct GraphNode
    {
        juce::Rectangle<float> worldBounds;
        juce::String title, detail, tab;
        int layer = -2; // -1 main, >=0 stored layer, -2 global/no instance selection
    };

    RetroMatchSynthAudioProcessor& proc;
    static constexpr int size = 2048;
    std::array<float, size> left {}, right {};
    std::array<float, AudioVisualBuffer::capacity> incomingLeft {}, incomingRight {};
    std::array<float, size * 2> fftData {};
    std::array<float, 48> bands {};
    int writeIndex = 0;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { size, juce::dsp::WindowingFunction<float>::hann, true };

    juce::Rectangle<float> graphViewport;
    std::vector<GraphNode> nodes;
    float graphZoom = 0.82f;
    juce::Point<float> graphPan { 18.0f, 18.0f };
    bool panning = false;
    juce::Point<float> panDragStart, panAtDragStart;
    int selectedNode = -1;

    float parameter (const juce::String& id, float fallback = 0.0f) const
    {
        if (auto* value = proc.apvts.getRawParameterValue (id)) return value->load();
        return fallback;
    }

    juce::Point<float> toScreen (juce::Point<float> world) const
    {
        return graphViewport.getPosition() + graphPan + world * graphZoom;
    }

    juce::Rectangle<float> toScreen (juce::Rectangle<float> world) const
    {
        const auto p = toScreen (world.getPosition());
        return { p.x, p.y, world.getWidth() * graphZoom, world.getHeight() * graphZoom };
    }

    juce::Point<float> toWorld (juce::Point<float> screenPoint) const
    {
        return (screenPoint - graphViewport.getPosition() - graphPan) / graphZoom;
    }

    int hitTestNode (juce::Point<float> screenPoint) const
    {
        const auto world = toWorld (screenPoint);
        for (int i = (int) nodes.size() - 1; i >= 0; --i)
            if (nodes[(size_t) i].worldBounds.contains (world)) return i;
        return -1;
    }

    void navigateTo (const GraphNode& node)
    {
        if (node.layer >= -1)
        {
            if (node.layer < 0 || proc.hasLayer (node.layer)) proc.selectEditingLayer (node.layer);
        }
        for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
        {
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (c))
            {
                const int index = tabs->getTabNames().indexOf (node.tab);
                if (index >= 0) { tabs->setCurrentTabIndex (index); break; }
            }
        }
    }

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

    void drawScope (juce::Graphics& g, juce::Rectangle<float> scope, juce::Colour led, juce::Colour accent)
    {
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
    }

    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> spectrum, juce::Colour led, juce::Colour accent)
    {
        for (int band = 0; band < 48; ++band)
        {
            const float x = spectrum.getX() + band * spectrum.getWidth() / 48.0f;
            const float h = bands[(size_t) band] * spectrum.getHeight();
            auto bar = juce::Rectangle<float> (x, spectrum.getBottom() - h, std::max (1.0f, spectrum.getWidth() / 48 - 1), h);
            g.setColour (accent.withAlpha (0.12f)); g.fillRect (bar.expanded (1.5f));
            g.setGradientFill (juce::ColourGradient (accent, bar.getTopLeft(), led.withAlpha (0.2f), bar.getBottomLeft(), false)); g.fillRect (bar);
        }
    }

    void drawStereo (juce::Graphics& g, juce::Rectangle<float> stereo, juce::Colour led)
    {
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
    }

    void drawPatchMap (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour led, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xff03080b)); g.fillRoundedRectangle (bounds, 7);
        g.setColour (juce::Colour (0xff506166)); g.drawRoundedRectangle (bounds, 7, 1);
        auto header = bounds.removeFromTop (26);
        g.setColour (led); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        const bool daw = parameter ("tempoSource", 1.0f) >= 0.5f;
        g.drawText ("PATCH MAP / DRAG EMPTY SPACE TO PAN / WHEEL TO ZOOM / DOUBLE-CLICK NODE TO EDIT    CLOCK: " + juce::String (proc.getEffectiveBpm(), 1) + " BPM " + (daw ? "DAW" : "MANUAL"), header.reduced (8, 0), juce::Justification::centredLeft);
        graphViewport = bounds.reduced (4);
        g.saveState(); g.reduceClipRegion (graphViewport.toNearestInt());
        nodes.clear();

        std::vector<int> instances { -1 };
        for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
            if (proc.hasLayer (i) && parameter ("layer" + juce::String (i + 1) + "Enabled") >= 0.5f) instances.push_back (i);

        const float rowGap = 74.0f, nodeW = 104.0f, nodeH = 42.0f;
        const std::array<float, 7> xs {{ 0, 122, 244, 366, 488, 610, 732 }};
        const juce::String stages[] { "INSTANCE", "OSC / WT", "6-OP FM", "FILTER / AMP", "MOD", "FX", "COMBINE" };
        const juce::String tabs[] { "SYNTH", "SYNTH", "FM", "FILTER + AMP", "MOD", "FX", "LAYERS" };
        const juce::uint32 colours[] { 0xff54f5d1, 0xffffbd65, 0xffc9a0ff, 0xff78f1c4, 0xffff91b8, 0xffa6cf75, 0xff94aaff, 0xffff9673 };

        auto addNode = [this, &g, led, accent] (juce::Rectangle<float> world, const juce::String& title, const juce::String& detail, const juce::String& tab, int layer, juce::Colour colour)
        {
            const int index = (int) nodes.size(); nodes.push_back ({ world, title, detail, tab, layer });
            auto r = toScreen (world);
            g.setColour (juce::Colour (0xff142226)); g.fillRoundedRectangle (r, 5.0f * graphZoom);
            g.setColour ((index == selectedNode ? juce::Colours::white : colour).withAlpha (0.72f)); g.drawRoundedRectangle (r, 5.0f * graphZoom, juce::jmax (1.0f, 1.3f * graphZoom));
            g.setColour (colour); g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 10.0f * graphZoom), juce::Font::bold)));
            g.drawFittedText (title, r.reduced (6 * graphZoom, 2 * graphZoom).removeFromTop (r.getHeight() * 0.53f).toNearestInt(), juce::Justification::centredLeft, 1);
            g.setColour (accent.withAlpha (0.82f)); g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 8.5f * graphZoom))));
            g.drawFittedText (detail, r.reduced (6 * graphZoom, 2 * graphZoom).withTrimmedTop (r.getHeight() * 0.48f).toNearestInt(), juce::Justification::centredLeft, 1);
        };

        for (size_t row = 0; row < instances.size(); ++row)
        {
            const int layer = instances[row]; const float y = (float) row * rowGap;
            const auto colour = juce::Colour (colours[row % std::size (colours)]);
            juce::String instanceDetail = layer < 0 ? "MAIN / level " + juce::String (parameter ("mainLayerGain", 1.0f), 2)
                                                    : "SYNTH " + juce::String (layer + 2) + " / level " + juce::String (parameter ("layer" + juce::String (layer + 1) + "Gain", 0.5f), 2);
            const int op = layer < 0 ? -1 : (int) parameter ("layer" + juce::String (layer + 1) + "Operation", 0.0f);
            const juce::String opNames[] { "ADD", "MIX", "SUBTRACT", "MULTIPLY", "DIVIDE" };
            const juce::String combineDetail = layer < 0 ? "MASTER START" : opNames[juce::jlimit (0, 4, op)] + " / " + juce::String (parameter ("layer" + juce::String (layer + 1) + "Amount", 1.0f), 2);
            const juce::String details[] { instanceDetail, "sources + tables", "algorithm + ops", "cutoff + ADSR", "LFO / MSEG / routes", "pre + built-in + post", combineDetail };
            for (int stage = 0; stage < 7; ++stage)
            {
                const juce::Rectangle<float> world (xs[(size_t) stage], y, nodeW, nodeH);
                if (stage > 0)
                {
                    juce::Path wire; wire.startNewSubPath (toScreen ({ xs[(size_t) stage - 1] + nodeW, y + nodeH * 0.5f })); wire.lineTo (toScreen ({ xs[(size_t) stage], y + nodeH * 0.5f }));
                    glow (g, wire, colour.withAlpha (0.6f), juce::jmax (0.8f, graphZoom));
                }
                addNode (world, stage == 0 ? (layer < 0 ? "INSTANCE 1 / MAIN" : "INSTANCE " + juce::String (layer + 2)) : stages[stage], details[stage], tabs[stage], layer, colour);
            }
        }

        const float busX = 860.0f;
        const float firstY = nodeH * 0.5f;
        const float lastY = (instances.size() - 1) * rowGap + nodeH * 0.5f;
        juce::Path bus; bus.startNewSubPath (toScreen ({ xs.back() + nodeW, firstY })); bus.lineTo (toScreen ({ busX, firstY })); bus.lineTo (toScreen ({ busX, lastY }));
        for (size_t row = 1; row < instances.size(); ++row) { const float y = row * rowGap + nodeH * 0.5f; bus.startNewSubPath (toScreen ({ xs.back() + nodeW, y })); bus.lineTo (toScreen ({ busX, y })); }
        const float masterY = (firstY + lastY) * 0.5f;
        bus.startNewSubPath (toScreen ({ busX, masterY })); bus.lineTo (toScreen ({ 888.0f, masterY })); glow (g, bus, led, juce::jmax (1.0f, graphZoom));
        addNode ({ 888.0f, masterY - nodeH * 0.5f, 118.0f, nodeH }, "MASTER OUT", juce::String (parameter ("outputGain", -3.0f), 1) + " dB", "FX", -2, led);
        addNode ({ 888.0f, masterY + 56.0f, 118.0f, nodeH }, "TEMPO CLOCK", juce::String (proc.getEffectiveBpm(), 1) + " BPM", "MOD", -2, accent);

        g.restoreState();
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
