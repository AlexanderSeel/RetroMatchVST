#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include <cmath>
#include <map>
#include <string>
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
        drawPatchMap (g, area, led, accent);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const auto action = hitToolbar (e.position); action != ToolbarAction::none)
        {
            handleToolbar (action);
            return;
        }

        if (! graphViewport.contains (e.position)) return;
        const int hit = hitTestNode (e.position);
        if (hit >= 0 && ! e.mods.isMiddleButtonDown() && ! e.mods.isRightButtonDown())
        {
            selectedNodeId = nodes[(size_t) hit].id.toStdString();
            draggingNode = true;
            draggingNodeId = selectedNodeId;
            nodeDragStartWorld = toWorld (e.position);
            const auto it = nodeOffsets.find (draggingNodeId);
            nodeOffsetAtDragStart = it != nodeOffsets.end() ? it->second : juce::Point<float>();
        }
        else
        {
            selectedNodeId.clear();
            panning = true;
            panDragStart = e.position;
            panAtDragStart = graphPan;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingNode)
        {
            auto offset = nodeOffsetAtDragStart + (toWorld (e.position) - nodeDragStartWorld);
            if (snapToGrid && ! e.mods.isShiftDown())
            {
                offset.x = std::round (offset.x / gridSize) * gridSize;
                offset.y = std::round (offset.y / gridSize) * gridSize;
            }
            nodeOffsets[draggingNodeId] = offset;
            repaint();
            return;
        }
        if (panning)
        {
            graphPan = panAtDragStart + (e.position - panDragStart);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        panning = false;
        draggingNode = false;
        draggingNodeId.clear();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! graphViewport.contains (e.position)) return;
        const int hit = hitTestNode (e.position);
        if (hit >= 0 && juce::isPositiveAndBelow (hit, (int) nodes.size()))
        {
            selectedNodeId = nodes[(size_t) hit].id.toStdString();
            navigateTo (nodes[(size_t) hit]);
        }
        else
        {
            fitToView();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! graphViewport.contains (e.position)) return;
        setZoomAround (e.position, graphZoom * (1.0f + wheel.deltaY * 0.18f));
    }

private:
    enum class EdgeKind { audio, modulation, clock };
    enum class ToolbarAction { none, autoArrange, fit, zoomOut, zoom100, zoomIn, grid };

    struct GraphNode
    {
        juce::String id;
        juce::Rectangle<float> baseBounds, worldBounds;
        juce::String title, detail, tab;
        int layer = -2;
        juce::Colour colour;
        bool inputPort = true, outputPort = true;
    };

    struct GraphEdge { int from = -1, to = -1; EdgeKind kind = EdgeKind::audio; };
    struct ToolbarButton { juce::Rectangle<float> bounds; juce::String label; ToolbarAction action = ToolbarAction::none; };

    RetroMatchSynthAudioProcessor& proc;
    static constexpr int size = 2048;
    static constexpr float gridSize = 12.0f;
    std::array<float, size> left {}, right {};
    std::array<float, AudioVisualBuffer::capacity> incomingLeft {}, incomingRight {};
    std::array<float, size * 2> fftData {};
    std::array<float, 48> bands {};
    int writeIndex = 0;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { size, juce::dsp::WindowingFunction<float>::hann, true };

    juce::Rectangle<float> graphViewport;
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::vector<ToolbarButton> toolbarButtons;
    std::map<std::string, juce::Point<float>> nodeOffsets;
    float graphZoom = 0.82f;
    juce::Point<float> graphPan { 18.0f, 18.0f };
    bool panning = false, draggingNode = false, snapToGrid = true;
    juce::Point<float> panDragStart, panAtDragStart, nodeDragStartWorld, nodeOffsetAtDragStart;
    std::string selectedNodeId, draggingNodeId;

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

    ToolbarAction hitToolbar (juce::Point<float> point) const
    {
        for (const auto& button : toolbarButtons)
            if (button.bounds.contains (point)) return button.action;
        return ToolbarAction::none;
    }

    void handleToolbar (ToolbarAction action)
    {
        switch (action)
        {
            case ToolbarAction::autoArrange:
                nodeOffsets.clear();
                for (auto& node : nodes) node.worldBounds = node.baseBounds;
                fitToView();
                break;
            case ToolbarAction::fit: fitToView(); break;
            case ToolbarAction::zoomOut: setZoomAround (graphViewport.getCentre(), graphZoom / 1.18f); break;
            case ToolbarAction::zoom100: setZoomAround (graphViewport.getCentre(), 1.0f); break;
            case ToolbarAction::zoomIn: setZoomAround (graphViewport.getCentre(), graphZoom * 1.18f); break;
            case ToolbarAction::grid: snapToGrid = ! snapToGrid; repaint(); break;
            case ToolbarAction::none: break;
        }
    }

    void setZoomAround (juce::Point<float> screenPoint, float requestedZoom)
    {
        if (graphViewport.isEmpty()) return;
        const auto before = toWorld (screenPoint);
        graphZoom = juce::jlimit (0.35f, 2.2f, requestedZoom);
        graphPan = screenPoint - graphViewport.getPosition() - before * graphZoom;
        repaint();
    }

    void fitToView()
    {
        if (nodes.empty() || graphViewport.isEmpty())
        {
            graphZoom = 0.82f;
            graphPan = { 18.0f, 18.0f };
            repaint();
            return;
        }
        auto world = nodes.front().worldBounds;
        for (size_t i = 1; i < nodes.size(); ++i) world = world.getUnion (nodes[i].worldBounds);
        world = world.expanded (28.0f);
        const float sx = graphViewport.getWidth() / juce::jmax (1.0f, world.getWidth());
        const float sy = graphViewport.getHeight() / juce::jmax (1.0f, world.getHeight());
        graphZoom = juce::jlimit (0.35f, 1.65f, juce::jmin (sx, sy));
        graphPan = graphViewport.getCentre() - graphViewport.getPosition() - world.getCentre() * graphZoom;
        repaint();
    }

    void navigateTo (const GraphNode& node)
    {
        if (node.layer >= -1 && (node.layer < 0 || proc.hasLayer (node.layer))) proc.selectEditingLayer (node.layer);
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

    int addNode (juce::Rectangle<float> base, const juce::String& id, const juce::String& title,
                 const juce::String& detail, const juce::String& tab, int layer, juce::Colour colour,
                 bool inputPort = true, bool outputPort = true)
    {
        auto offset = juce::Point<float>();
        if (const auto it = nodeOffsets.find (id.toStdString()); it != nodeOffsets.end()) offset = it->second;
        nodes.push_back ({ id, base, base.translated (offset.x, offset.y), title, detail, tab, layer, colour, inputPort, outputPort });
        return (int) nodes.size() - 1;
    }

    void connect (int from, int to, EdgeKind kind = EdgeKind::audio)
    {
        if (juce::isPositiveAndBelow (from, (int) nodes.size()) && juce::isPositiveAndBelow (to, (int) nodes.size()))
            edges.push_back ({ from, to, kind });
    }

    void drawGrid (juce::Graphics& g, juce::Colour led)
    {
        const auto topLeft = toWorld (graphViewport.getTopLeft());
        const auto bottomRight = toWorld (graphViewport.getBottomRight());
        const float step = gridSize * 2.0f;
        const float x0 = std::floor (topLeft.x / step) * step;
        const float y0 = std::floor (topLeft.y / step) * step;
        g.setColour (led.withAlpha (snapToGrid ? 0.07f : 0.035f));
        for (float x = x0; x <= bottomRight.x + step; x += step)
        {
            const float sx = toScreen ({ x, 0.0f }).x;
            g.drawVerticalLine ((int) sx, graphViewport.getY(), graphViewport.getBottom());
        }
        for (float y = y0; y <= bottomRight.y + step; y += step)
        {
            const float sy = toScreen ({ 0.0f, y }).y;
            g.drawHorizontalLine ((int) sy, graphViewport.getX(), graphViewport.getRight());
        }
    }

    void drawEdge (juce::Graphics& g, const GraphEdge& edge, juce::Colour led, juce::Colour accent)
    {
        if (! juce::isPositiveAndBelow (edge.from, (int) nodes.size()) || ! juce::isPositiveAndBelow (edge.to, (int) nodes.size())) return;
        const auto& from = nodes[(size_t) edge.from];
        const auto& to = nodes[(size_t) edge.to];
        juce::Point<float> a, b;
        if (edge.kind == EdgeKind::modulation)
        {
            a = toScreen ({ from.worldBounds.getCentreX(), from.worldBounds.getY() });
            b = toScreen ({ to.worldBounds.getCentreX(), to.worldBounds.getBottom() });
        }
        else if (edge.kind == EdgeKind::clock)
        {
            a = toScreen ({ from.worldBounds.getX(), from.worldBounds.getCentreY() });
            b = toScreen ({ to.worldBounds.getCentreX(), to.worldBounds.getBottom() });
        }
        else
        {
            a = toScreen ({ from.worldBounds.getRight(), from.worldBounds.getCentreY() });
            b = toScreen ({ to.worldBounds.getX(), to.worldBounds.getCentreY() });
        }
        const float bend = juce::jmax (24.0f, std::abs (b.x - a.x) * 0.42f);
        juce::Path wire;
        wire.startNewSubPath (a);
        wire.cubicTo ({ a.x + bend, a.y }, { b.x - bend, b.y }, b);
        if (edge.kind == EdgeKind::audio)
            glow (g, wire, from.colour.interpolatedWith (to.colour, 0.45f).withAlpha (0.7f), juce::jmax (0.9f, graphZoom));
        else
        {
            const auto colour = edge.kind == EdgeKind::clock ? accent : led;
            g.setColour (colour.withAlpha (edge.kind == EdgeKind::clock ? 0.32f : 0.24f));
            g.strokePath (wire, juce::PathStrokeType (juce::jmax (0.7f, graphZoom * 0.85f)));
        }
    }

    void drawNode (juce::Graphics& g, const GraphNode& node, juce::Colour accent)
    {
        auto r = toScreen (node.worldBounds);
        const bool selected = node.id.toStdString() == selectedNodeId;
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (r.translated (0, 2.5f * graphZoom), 5.0f * graphZoom);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17272b), r.getTopLeft(), juce::Colour (0xff0d171a), r.getBottomRight(), false));
        g.fillRoundedRectangle (r, 5.0f * graphZoom);
        g.setColour ((selected ? juce::Colours::white : node.colour).withAlpha (selected ? 0.95f : 0.72f));
        g.drawRoundedRectangle (r, 5.0f * graphZoom, juce::jmax (1.0f, 1.3f * graphZoom));
        g.setColour (node.colour); g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 10.0f * graphZoom), juce::Font::bold)));
        g.drawFittedText (node.title, r.reduced (6 * graphZoom, 2 * graphZoom).removeFromTop (r.getHeight() * 0.53f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (accent.withAlpha (0.82f)); g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 8.5f * graphZoom))));
        g.drawFittedText (node.detail, r.reduced (6 * graphZoom, 2 * graphZoom).withTrimmedTop (r.getHeight() * 0.48f).toNearestInt(), juce::Justification::centredLeft, 1);

        const float portRadius = juce::jmax (2.3f, 3.1f * graphZoom);
        g.setColour (node.colour.withAlpha (0.9f));
        if (node.inputPort) g.fillEllipse (r.getX() - portRadius, r.getCentreY() - portRadius, portRadius * 2, portRadius * 2);
        if (node.outputPort) g.fillEllipse (r.getRight() - portRadius, r.getCentreY() - portRadius, portRadius * 2, portRadius * 2);
    }

    void drawToolbar (juce::Graphics& g, juce::Rectangle<float> header, juce::Colour led, juce::Colour accent)
    {
        toolbarButtons.clear();
        const float gap = 4.0f, h = 20.0f;
        struct Def { const char* label; float width; ToolbarAction action; };
        const Def defs[] {{ "AUTO", 48, ToolbarAction::autoArrange }, { "FIT", 40, ToolbarAction::fit },
                          { "-", 26, ToolbarAction::zoomOut }, { "100%", 42, ToolbarAction::zoom100 },
                          { "+", 26, ToolbarAction::zoomIn }, { "GRID", 48, ToolbarAction::grid }};
        float total = -gap;
        for (const auto& d : defs) total += d.width + gap;
        float x = header.getRight() - total - 6.0f;
        for (const auto& d : defs)
        {
            juce::Rectangle<float> r (x, header.getCentreY() - h * 0.5f, d.width, h);
            toolbarButtons.push_back ({ r, d.label, d.action });
            const bool active = d.action == ToolbarAction::grid && snapToGrid;
            g.setColour (active ? led.withAlpha (0.18f) : juce::Colour (0xff132126)); g.fillRoundedRectangle (r, 4);
            g.setColour ((active ? led : accent).withAlpha (0.8f)); g.drawRoundedRectangle (r, 4, 1);
            g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold))); g.setColour (active ? led : juce::Colour (0xffb7c5c8));
            g.drawText (d.label, r, juce::Justification::centred);
            x += d.width + gap;
        }

        auto textArea = header.withTrimmedRight (total + 12.0f).reduced (8, 0);
        g.setColour (led); g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        const bool daw = parameter ("tempoSource", 1.0f) >= 0.5f;
        g.drawFittedText ("PATCH MAP / DRAG NODE TO MOVE / EMPTY SPACE TO PAN / WHEEL TO ZOOM / DOUBLE-CLICK TO EDIT    CLOCK: "
                          + juce::String (proc.getEffectiveBpm(), 1) + " BPM " + (daw ? "DAW" : "MANUAL"),
                          textArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void drawPatchMap (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour led, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xff03080b)); g.fillRoundedRectangle (bounds, 7);
        g.setColour (juce::Colour (0xff506166)); g.drawRoundedRectangle (bounds, 7, 1);
        auto header = bounds.removeFromTop (30);
        graphViewport = bounds.reduced (4);

        nodes.clear(); edges.clear();
        std::vector<int> instances { -1 };
        for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
            if (proc.hasLayer (i) && parameter ("layer" + juce::String (i + 1) + "Enabled") >= 0.5f) instances.push_back (i);

        const float rowGap = 112.0f, nodeW = 104.0f, nodeH = 42.0f;
        const std::array<float, 6> xs {{ 0, 122, 244, 366, 488, 610 }};
        const juce::String stages[] { "INSTANCE", "OSC / WT", "6-OP FM", "FILTER / AMP", "FX", "COMBINE" };
        const juce::String tabs[] { "SYNTH", "SYNTH", "FM", "FILTER + AMP", "FX", "LAYERS" };
        const juce::uint32 colours[] { 0xff54f5d1, 0xffffbd65, 0xffc9a0ff, 0xff78f1c4, 0xffff91b8, 0xffa6cf75, 0xff94aaff, 0xffff9673 };
        std::vector<int> combineNodes, modNodes;

        for (size_t row = 0; row < instances.size(); ++row)
        {
            const int layer = instances[row]; const float y = (float) row * rowGap;
            const auto colour = juce::Colour (colours[row % std::size (colours)]);
            const juce::String prefix = "L" + juce::String (layer);
            const juce::String instanceDetail = layer < 0 ? "MAIN / level " + juce::String (parameter ("mainLayerGain", 1.0f), 2)
                                                           : "SYNTH " + juce::String (layer + 2) + " / level " + juce::String (parameter ("layer" + juce::String (layer + 1) + "Gain", 0.5f), 2);
            const int op = layer < 0 ? -1 : (int) parameter ("layer" + juce::String (layer + 1) + "Operation", 0.0f);
            const juce::String opNames[] { "ADD", "MIX", "SUBTRACT", "MULTIPLY", "DIVIDE" };
            const juce::String combineDetail = layer < 0 ? "MASTER START" : opNames[juce::jlimit (0, 4, op)] + " / " + juce::String (parameter ("layer" + juce::String (layer + 1) + "Amount", 1.0f), 2);
            const juce::String details[] { instanceDetail, "sources + tables", "algorithm + ops", "cutoff + ADSR", "pre + built-in + post", combineDetail };
            std::array<int, 6> rowNodes {};
            for (int stage = 0; stage < 6; ++stage)
            {
                rowNodes[(size_t) stage] = addNode ({ xs[(size_t) stage], y, nodeW, nodeH }, prefix + ":S" + juce::String (stage),
                    stage == 0 ? (layer < 0 ? "INSTANCE 1 / MAIN" : "INSTANCE " + juce::String (layer + 2)) : stages[stage],
                    details[stage], tabs[stage], layer, colour, stage != 0, true);
                if (stage > 0) connect (rowNodes[(size_t) stage - 1], rowNodes[(size_t) stage], EdgeKind::audio);
            }
            combineNodes.push_back (rowNodes.back());

            const int mod = addNode ({ 305.0f, y + 58.0f, 126.0f, 36.0f }, prefix + ":MOD", "MOD / ROUTES", "LFO / MSEG / matrix", "MOD", layer, accent, false, false);
            modNodes.push_back (mod);
            connect (mod, rowNodes[1], EdgeKind::modulation);
            connect (mod, rowNodes[2], EdgeKind::modulation);
            connect (mod, rowNodes[3], EdgeKind::modulation);
            connect (mod, rowNodes[4], EdgeKind::modulation);
        }

        const float firstY = nodeH * 0.5f;
        const float lastY = (instances.size() - 1) * rowGap + nodeH * 0.5f;
        const float masterY = (firstY + lastY) * 0.5f;
        const int master = addNode ({ 770.0f, masterY - nodeH * 0.5f, 126.0f, nodeH }, "MASTER", "MASTER OUT",
                                    juce::String (parameter ("masterOutputGain", 0.0f), 1) + " dB", "FX", -2, led, true, false);
        for (const auto combine : combineNodes) connect (combine, master, EdgeKind::audio);
        const int clock = addNode ({ 770.0f, masterY + 64.0f, 126.0f, nodeH }, "CLOCK", "TEMPO CLOCK",
                                   juce::String (proc.getEffectiveBpm(), 1) + " BPM", "MOD", -2, accent, false, true);
        for (const auto mod : modNodes) connect (clock, mod, EdgeKind::clock);

        drawToolbar (g, header, led, accent);
        g.saveState(); g.reduceClipRegion (graphViewport.toNearestInt());
        drawGrid (g, led);
        for (const auto& edge : edges) drawEdge (g, edge, led, accent);
        for (const auto& node : nodes) drawNode (g, node, accent);
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
