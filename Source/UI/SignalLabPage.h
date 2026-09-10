#pragma once
#include "../PluginProcessor.h"
#include "RetroLookAndFeel.h"
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class SignalLabPage final : public juce::Component, private juce::Timer
{
    class LargePatchMapWindow final : public juce::DialogWindow
    {
    public:
        explicit LargePatchMapWindow (float desktopScale)
            : juce::DialogWindow ("RM-01 / LARGE PATCH MAP",
                                  juce::Colour (0xff101719), true, true, desktopScale)
        {
        }

        void closeButtonPressed() override
        {
            exitModalState (0);
        }
    };

    static constexpr float kReadableGraphZoom = 0.50f;

public:
    explicit SignalLabPage (RetroMatchSynthAudioProcessor& p, bool mapOnly = false) : proc (p), mapOnlyMode (mapOnly)
    {
        const auto restored = proc.getPatchGraphDocument();
        graphPan = { restored.view.pan.x, restored.view.pan.y };
        graphZoom = juce::jlimit (kReadableGraphZoom, 2.2f, restored.view.zoom);
        snapToGrid = restored.view.snapToGrid;
        lastPersistedGraphFingerprint = restored.fingerprint();
        for (const auto& node : restored.nodes)
        {
            // The BIG map is a presentation/editor surface, not a magnified copy
            // of the compact map's persisted manual geometry. Start it from the
            // canonical signal-flow layout so a displaced compact node cannot
            // create metres of dead space in the large window.
            if (! mapOnlyMode && node.positionValid)
                restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };
            if (node.routingMode != 0) restoredNodeRoutingModes[node.id.toStdString()] = node.routingMode;
            if (node.locked) restoredNodeLocks[node.id.toStdString()] = true;
        }
        for (const auto& edge : restored.edges)
            if (edge.sequence >= 0) restoredEdgeSequences[edge.id.toStdString()] = edge.sequence;
        setWantsKeyboardFocus (true);
        bigViewNeedsFit = mapOnlyMode;
        startTimerHz (30);
    }

    void resized() override
    {
        if (mapOnlyMode)
        {
            bigViewNeedsFit = true;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101719));
        auto area = getLocalBounds().toFloat().reduced (mapOnlyMode ? 8.0f : 16.0f);
        const auto led = findColour (RetroLookAndFeel::primaryLed), accent = findColour (RetroLookAndFeel::secondaryLed);
        if (mapOnlyMode)
        {
            drawPatchMap (g, area, led, accent);
            return;
        }
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
        grabKeyboardFocus();

        if (e.mods.isRightButtonDown())
        {
            if (const int edge = hitTestEdge (e.position); edge >= 0)
            {
                selectedEdgeKey = edges[(size_t) edge].key.toStdString();
                showEdgeMenu (edges[(size_t) edge]);
                repaint();
                return;
            }

            if (const int node = hitTestNode (e.position); node >= 0)
            {
                selectedNodeId = nodes[(size_t) node].id.toStdString();
                if (nodes[(size_t) node].role == NodeRole::modHub) showModHubMenu (nodes[(size_t) node].layer);
                else if (nodes[(size_t) node].role == NodeRole::stage && nodes[(size_t) node].stage == 0) showInstanceMenu (nodes[(size_t) node].layer);
                else if (nodes[(size_t) node].role == NodeRole::stage && nodes[(size_t) node].stage == 4) showFxTopologyMenu (nodes[(size_t) node]);
                else showNodeLockMenu (nodes[(size_t) node]);
                repaint();
            }
            return;
        }

        if (! e.mods.isMiddleButtonDown())
        {
            if (const int edgeIndex = hitTestEdge (e.position); edgeIndex >= 0 && edges[(size_t) edgeIndex].editKind != EdgeEditKind::none)
            {
                const auto& edge = edges[(size_t) edgeIndex];
                selectedEdgeKey = edge.key.toStdString();
                selectedNodeId.clear();
                if (edge.editKind == EdgeEditKind::modulationRoute && isReconnectDestinationHit (edge, e.position))
                {
                    const auto voice = voiceForLayer (edge.layer);
                    if (juce::isPositiveAndBelow (edge.slot, VoiceParameters::modGraphSlotCount))
                    {
                        const auto route = voice.moduleModSlots[(size_t) edge.slot];
                        reconnectLayer = edge.layer; reconnectSlot = edge.slot;
                        reconnectSource = route.source; reconnectAmount = route.amount;
                        connecting = true;
                        connectionFromId = nodes[(size_t) edge.from].id.toStdString();
                        connectionDragPoint = e.position;
                        hoverTargetId.clear();
                        connectionValidationMessage.clear();
                    }
                }
                repaint();
                return;
            }
            if (const int output = hitTestModOutput (e.position); output >= 0)
            {
                connectionValidationMessage.clear();
                reconnectLayer = -2; reconnectSlot = -1;
                reconnectSource = (int) ModSource::none; reconnectAmount = 0.5f;
                connecting = true;
                connectionFromId = nodes[(size_t) output].id.toStdString();
                connectionDragPoint = e.position;
                hoverTargetId.clear();
                selectedNodeId = connectionFromId;
                selectedEdgeKey.clear();
                repaint();
                return;
            }
        }

        const int hit = hitTestNode (e.position);
        if (hit >= 0 && ! e.mods.isMiddleButtonDown())
        {
            const auto hitId = nodes[(size_t) hit].id.toStdString();
            const bool additiveSelection = e.mods.isCommandDown() || e.mods.isCtrlDown();
            if (additiveSelection)
            {
                if (selectedNodeIds.count (hitId) != 0) selectedNodeIds.erase (hitId);
                else selectedNodeIds.insert (hitId);
                selectedNodeId = hitId;
            }
            else
            {
                selectedNodeIds.clear();
                selectedNodeIds.insert (hitId);
                selectedNodeId = hitId;
            }
            selectedEdgeKey.clear();
            if (nodes[(size_t) hit].locked || selectedNodeIds.empty())
            {
                repaint();
                return;
            }
            draggingNode = true;
            draggingNodeId = selectedNodeId;
            nodeDragStartWorld = toWorld (e.position);
            groupNodeOffsetsAtDragStart.clear();
            for (const auto& id : selectedNodeIds)
            {
                const auto it = nodeOffsets.find (id);
                if (it != nodeOffsets.end()) groupNodeOffsetsAtDragStart[id] = it->second;
            }
            if (groupNodeOffsetsAtDragStart.empty())
                groupNodeOffsetsAtDragStart[draggingNodeId] = {};
        }
        else
        {
            selectedNodeId.clear();
            selectedNodeIds.clear();
            selectedEdgeKey.clear();
            panning = true;
            panDragStart = e.position;
            panAtDragStart = graphPan;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (connecting)
        {
            connectionDragPoint = e.position;
            hoverTargetId.clear();
            if (const int target = hitTestModInput (e.position); target >= 0)
            {
                hoverTargetId = nodes[(size_t) target].id.toStdString();
                const int from = findNodeIndex (connectionFromId);
                if (from >= 0)
                {
                    const auto validation = validateModConnection (nodes[(size_t) from], nodes[(size_t) target]);
                    connectionValidationMessage = validation.ok ? juce::String {} : validation.message;
                }
            }
            else
            {
                connectionValidationMessage = "Drop on a compatible MOD input in the same synth instance";
            }
            repaint();
            return;
        }

        if (draggingNode)
        {
            if (! dragHistoryCaptured)
            {
                pushUndoState();
                dragHistoryCaptured = true;
            }
            const auto delta = toWorld (e.position) - nodeDragStartWorld;
            for (const auto& [id, startOffset] : groupNodeOffsetsAtDragStart)
            {
                const auto nodeIndex = findNodeIndex (id);
                if (nodeIndex < 0 || nodes[(size_t) nodeIndex].locked) continue;
                auto offset = startOffset + delta;
                if (snapToGrid && ! e.mods.isShiftDown())
                {
                    offset.x = std::round (offset.x / gridSize) * gridSize;
                    offset.y = std::round (offset.y / gridSize) * gridSize;
                }
                nodeOffsets[id] = offset;
            }
            repaint();
            return;
        }
        if (panning)
        {
            graphPan = panAtDragStart + (e.position - panDragStart);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (connecting)
        {
            const auto fromId = connectionFromId;
            const auto targetId = hoverTargetId;
            const int reconnectLayerAtDrop = reconnectLayer;
            const int reconnectSlotAtDrop = reconnectSlot;
            const int reconnectSourceAtDrop = reconnectSource;
            const float reconnectAmountAtDrop = reconnectAmount;
            connecting = false;
            connectionFromId.clear();
            hoverTargetId.clear();
            reconnectLayer = -2; reconnectSlot = -1;
            reconnectSource = (int) ModSource::none; reconnectAmount = 0.5f;

            const int from = findNodeIndex (fromId);
            const int target = findNodeIndex (targetId);
            if (from >= 0 && target >= 0)
            {
                const auto validation = validateModConnection (nodes[(size_t) from], nodes[(size_t) target]);
                if (validation.ok)
                {
                    connectionValidationMessage.clear();
                    if (reconnectSlotAtDrop >= 0)
                        showReconnectRouteMenu (reconnectLayerAtDrop, reconnectSlotAtDrop, reconnectSourceAtDrop,
                                                reconnectAmountAtDrop, nodes[(size_t) target].stage);
                    else
                        showNewRouteMenu (nodes[(size_t) from].layer, nodes[(size_t) target].stage);
                }
                else
                {
                    connectionValidationMessage = validation.message;
                }
            }
            else
            {
                connectionValidationMessage = "Drop on a compatible MOD input in the same synth instance";
            }

            connectionDragPoint = e.position;
            stageGraphStateForPersistence();
            repaint();
            return;
        }

        panning = false;
        draggingNode = false;
        draggingNodeId.clear();
        groupNodeOffsetsAtDragStart.clear();
        dragHistoryCaptured = false;
        stageGraphStateForPersistence();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! graphViewport.contains (e.position)) return;

        if (const int edge = hitTestEdge (e.position); edge >= 0)
        {
            selectedEdgeKey = edges[(size_t) edge].key.toStdString();
            showEdgeMenu (edges[(size_t) edge]);
            repaint();
            return;
        }

        const int hit = hitTestNode (e.position);
        if (hit >= 0 && juce::isPositiveAndBelow (hit, (int) nodes.size()))
        {
        selectedNodeId = nodes[(size_t) hit].id.toStdString();
        selectedNodeIds.clear();
        selectedNodeIds.insert (selectedNodeId);
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

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto keyCode = key.getKeyCode();
        const auto text = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        const bool command = key.getModifiers().isCommandDown();
        if (command && text == 'z')
            return key.getModifiers().isShiftDown() ? redoCableEdit() : undoCableEdit();
        if (command && text == 'y') return redoCableEdit();
        if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
            return deleteSelectedCable();
        if (keyCode == juce::KeyPress::escapeKey && connecting)
        {
            connecting = false;
            connectionFromId.clear(); hoverTargetId.clear();
            reconnectLayer = -2; reconnectSlot = -1;
            connectionValidationMessage.clear();
            repaint();
            return true;
        }
        return false;
    }

private:
    enum class EdgeKind { audio, modulation, clock };
    enum class EdgeEditKind { none, modulationRoute, layerCombine };
    enum class NodeRole { stage, modHub, master, clock };
    enum class ToolbarAction { none, undo, redo, autoArrange, fit, zoomOut, zoom100, zoomIn, grid, expand };

    struct GraphNode
    {
        juce::String id;
        juce::Rectangle<float> baseBounds, worldBounds;
        juce::String title, detail, tab;
        int layer = -2;
        juce::Colour colour;
        bool inputPort = true, outputPort = true;
        bool modInputPort = false, modOutputPort = false;
        bool locked = false;
        NodeRole role = NodeRole::stage;
        int stage = -1;
        int routingMode = 0;
    };

    struct GraphEdge
    {
        int from = -1, to = -1;
        EdgeKind kind = EdgeKind::audio;
        EdgeEditKind editKind = EdgeEditKind::none;
        int layer = -2, slot = -1;
        juce::String key, label;
        int sequence = -1;
    };

    struct ToolbarButton { juce::Rectangle<float> bounds; juce::String label; ToolbarAction action = ToolbarAction::none; };

    struct CableHistoryState
    {
        PatchGraph::Document graph;
        std::array<std::array<ModSlotParameters, VoiceParameters::modGraphSlotCount>, VoiceParameters::extraLayerCount + 1> routes {};
        std::array<bool, VoiceParameters::extraLayerCount> layerPresent {};
        std::array<float, VoiceParameters::extraLayerCount> layerEnabled {}, layerOperation {}, layerAmount {};
        int editingLayer = -1;
    };

    RetroMatchSynthAudioProcessor& proc;
    bool mapOnlyMode = false;
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
    std::map<std::string, juce::Point<float>> restoredNodePositions;
    std::map<std::string, bool> restoredNodeLocks;
    std::map<std::string, int> restoredNodeRoutingModes;
    std::map<std::string, int> restoredEdgeSequences;
    PatchGraph::Document graphModel;
    juce::String lastPersistedGraphFingerprint, pendingGraphFingerprint;
    juce::String graphValidationMessage, connectionValidationMessage;
    bool graphStateDirty = false;
    float graphZoom = 0.82f;
    juce::Point<float> graphPan { 18.0f, 18.0f };
    bool panning = false, draggingNode = false, snapToGrid = true, connecting = false;
    bool restoringHistory = false, dragHistoryCaptured = false, bigViewNeedsFit = false;
    juce::Point<float> panDragStart, panAtDragStart, nodeDragStartWorld, nodeOffsetAtDragStart, connectionDragPoint;
    std::string selectedNodeId, draggingNodeId, selectedEdgeKey, connectionFromId, hoverTargetId;
    std::set<std::string> selectedNodeIds;
    std::map<std::string, juce::Point<float>> groupNodeOffsetsAtDragStart;
    RoutingMeterSnapshot routingMeters;
    int reconnectLayer = -2, reconnectSlot = -1, reconnectSource = (int) ModSource::none;
    float reconnectAmount = 0.5f;
    std::vector<CableHistoryState> undoHistory, redoHistory;

    float parameter (const juce::String& id, float fallback = 0.0f) const
    {
        if (auto* value = proc.apvts.getRawParameterValue (id)) return value->load();
        return fallback;
    }

    void setParameterValue (const juce::String& id, float value)
    {
        if (auto* parameterObject = proc.apvts.getParameter (id))
        {
            parameterObject->beginChangeGesture();
            parameterObject->setValueNotifyingHost (parameterObject->convertTo0to1 (value));
            parameterObject->endChangeGesture();
        }
    }

    VoiceParameters voiceForLayer (int layer) const
    {
        if (layer < 0) return proc.getMainVoiceParameters();
        if (auto saved = proc.getLayerParameters (layer)) return *saved;
        return {};
    }

    static juce::String sourceName (int source)
    {
        static const juce::StringArray names { "OFF", "LFO 1", "VELOCITY", "KEY TRACK", "RANDOM NOTE", "AMP ENV", "MSEG", "LFO 2", "LFO 3", "LFO 4" };
        return names[juce::jlimit (0, names.size() - 1, source)];
    }

    static juce::String destinationName (int destination)
    {
        static const juce::StringArray names { "OFF", "PITCH", "CUTOFF", "AMPLITUDE", "PULSE WIDTH", "FM AMOUNT", "6-OP FM MIX", "WAVETABLE POSITION", "WAVEFOLD" };
        return names[juce::jlimit (0, names.size() - 1, destination)];
    }

    static int stageForDestination (int destination)
    {
        switch ((ModDestination) destination)
        {
            case ModDestination::pitch:
            case ModDestination::pulseWidth:
            case ModDestination::wavetablePosition:
            case ModDestination::wavefold: return 1;
            case ModDestination::fmAmount:
            case ModDestination::fmMix: return 2;
            case ModDestination::cutoff:
            case ModDestination::amplitude: return 3;
            case ModDestination::none: break;
        }
        return -1;
    }

    static std::vector<int> destinationsForStage (int stage)
    {
        std::vector<int> result;
        for (int destination = (int) ModDestination::pitch; destination <= (int) ModDestination::wavefold; ++destination)
            if (stage < 0 || stageForDestination (destination) == stage) result.push_back (destination);
        return result;
    }

    static juce::String stageName (int stage)
    {
        if (stage == 1) return "OSC / WT";
        if (stage == 2) return "6-OP FM";
        if (stage == 3) return "FILTER / AMP";
        return "SUPPORTED DESTINATION";
    }

    CableHistoryState captureCableHistoryState() const
    {
        CableHistoryState state;
        state.graph = graphModel;
        state.routes[0] = voiceForLayer (-1).moduleModSlots;
        state.editingLayer = proc.getEditingLayer();
        for (int layer = 0; layer < VoiceParameters::extraLayerCount; ++layer)
        {
            state.layerPresent[(size_t) layer] = proc.hasLayer (layer);
            if (! state.layerPresent[(size_t) layer]) continue;
            state.routes[(size_t) layer + 1] = voiceForLayer (layer).moduleModSlots;
            const auto prefix = "layer" + juce::String (layer + 1);
            state.layerEnabled[(size_t) layer] = parameter (prefix + "Enabled", 1.0f);
            state.layerOperation[(size_t) layer] = parameter (prefix + "Operation", 0.0f);
            state.layerAmount[(size_t) layer] = parameter (prefix + "Amount", 1.0f);
        }
        return state;
    }

    void pushUndoState()
    {
        if (restoringHistory) return;
        undoHistory.push_back (captureCableHistoryState());
        if (undoHistory.size() > 32) undoHistory.erase (undoHistory.begin());
        redoHistory.clear();
    }

    void restoreCableHistoryState (const CableHistoryState& state)
    {
        const juce::ScopedValueSetter<bool> restoring (restoringHistory, true);
        auto restoreRoutes = [&] (int layer, size_t historyIndex)
        {
            if (layer >= 0 && (! state.layerPresent[(size_t) layer] || ! proc.hasLayer (layer))) return;
            proc.selectEditingLayer (layer);
            for (int slot = 0; slot < VoiceParameters::modGraphSlotCount; ++slot)
            {
                const auto& route = state.routes[historyIndex][(size_t) slot];
                const auto prefix = "moduleMod" + juce::String (slot + 1);
                setParameterValue (prefix + "Source", (float) route.source);
                setParameterValue (prefix + "Dest", (float) route.destination);
                setParameterValue (prefix + "Amount", juce::jlimit (-1.0f, 1.0f, route.amount));
            }
            if (layer >= 0) proc.refreshEditingLayer();
        };

        restoreRoutes (-1, 0);
        for (int layer = 0; layer < VoiceParameters::extraLayerCount; ++layer)
        {
            if (! state.layerPresent[(size_t) layer] || ! proc.hasLayer (layer)) continue;
            restoreRoutes (layer, (size_t) layer + 1);
            const auto prefix = "layer" + juce::String (layer + 1);
            setParameterValue (prefix + "Enabled", state.layerEnabled[(size_t) layer]);
            setParameterValue (prefix + "Operation", state.layerOperation[(size_t) layer]);
            setParameterValue (prefix + "Amount", state.layerAmount[(size_t) layer]);
        }

        const int editing = state.editingLayer >= 0 && ! proc.hasLayer (state.editingLayer) ? -1 : state.editingLayer;
        proc.selectEditingLayer (editing);
        proc.setPatchGraphDocument (state.graph);
        graphPan = { state.graph.view.pan.x, state.graph.view.pan.y };
        graphZoom = state.graph.view.zoom;
        snapToGrid = state.graph.view.snapToGrid;
        restoredNodePositions.clear(); nodeOffsets.clear(); restoredNodeRoutingModes.clear(); restoredEdgeSequences.clear();
        for (const auto& node : state.graph.nodes)
        {
            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };
            if (node.routingMode != 0) restoredNodeRoutingModes[node.id.toStdString()] = node.routingMode;
        }
        for (const auto& edge : state.graph.edges)
            if (edge.sequence >= 0) restoredEdgeSequences[edge.id.toStdString()] = edge.sequence;
        lastPersistedGraphFingerprint = state.graph.fingerprint();
        pendingGraphFingerprint = lastPersistedGraphFingerprint;
        graphStateDirty = false;
        selectedEdgeKey.clear(); selectedNodeId.clear();
        connectionValidationMessage.clear();
        repaint();
    }

    bool undoCableEdit()
    {
        if (undoHistory.empty()) return false;
        redoHistory.push_back (captureCableHistoryState());
        auto state = std::move (undoHistory.back());
        undoHistory.pop_back();
        restoreCableHistoryState (state);
        return true;
    }

    bool redoCableEdit()
    {
        if (redoHistory.empty()) return false;
        undoHistory.push_back (captureCableHistoryState());
        auto state = std::move (redoHistory.back());
        redoHistory.pop_back();
        restoreCableHistoryState (state);
        return true;
    }

    void setRoute (int layer, int slot, int source, int destination, float amount)
    {
        if (! juce::isPositiveAndBelow (slot, VoiceParameters::modGraphSlotCount)) return;
        if (layer >= 0 && ! proc.hasLayer (layer)) return;
        if (! restoringHistory) pushUndoState();

        const int previousEditingLayer = proc.getEditingLayer();
        proc.selectEditingLayer (layer);
        const auto prefix = "moduleMod" + juce::String (slot + 1);
        setParameterValue (prefix + "Source", (float) source);
        setParameterValue (prefix + "Dest", (float) destination);
        setParameterValue (prefix + "Amount", juce::jlimit (-1.0f, 1.0f, amount));
        if (layer >= 0) proc.refreshEditingLayer();
        if (previousEditingLayer != layer)
        {
            const int restoreLayer = previousEditingLayer >= 0 && ! proc.hasLayer (previousEditingLayer) ? -1 : previousEditingLayer;
            proc.selectEditingLayer (restoreLayer);
        }
        selectedEdgeKey = ("route:" + juce::String (layer) + ":" + juce::String (slot)).toStdString();
        repaint();
    }

    void clearRoute (int layer, int slot)
    {
        setRoute (layer, slot, (int) ModSource::none, (int) ModDestination::none, 0.0f);
        selectedEdgeKey.clear();
    }

    int findFreeRouteSlot (int layer) const
    {
        const auto voice = voiceForLayer (layer);
        for (int i = 0; i < VoiceParameters::modGraphSlotCount; ++i)
        {
            const auto& route = voice.moduleModSlots[(size_t) i];
            if (route.source == (int) ModSource::none || route.destination == (int) ModDestination::none) return i;
        }
        return -1;
    }

    juce::String routeSummary (int layer, int slot) const
    {
        const auto voice = voiceForLayer (layer);
        if (! juce::isPositiveAndBelow (slot, VoiceParameters::modGraphSlotCount)) return "EMPTY";
        const auto& route = voice.moduleModSlots[(size_t) slot];
        if (route.source == (int) ModSource::none || route.destination == (int) ModDestination::none) return "EMPTY";
        return sourceName (route.source) + " > " + destinationName (route.destination) + "  " + juce::String (route.amount, 2);
    }

    void showCreateRouteMenu (int layer, int slot, int targetStage)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("ADD MOD ROUTE / " + stageName (targetStage));
        for (int source = (int) ModSource::lfo1; source <= (int) ModSource::lfo4; ++source)
        {
            juce::PopupMenu destinations;
            for (const int destination : destinationsForStage (targetStage))
                destinations.addItem (1000 + source * 32 + destination, destinationName (destination));
            menu.addSubMenu (sourceName (source), destinations);
        }

        const auto previous = voiceForLayer (layer).moduleModSlots[(size_t) slot];
        const float previousAmount = std::abs (previous.amount) > 0.001f ? previous.amount : 0.5f;
        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis, layer, slot, previousAmount] (int result)
        {
            if (safeThis == nullptr || result < 1000 || result >= 2000) return;
            const int packed = result - 1000;
            const int source = packed / 32;
            const int destination = packed % 32;
            safeThis->setRoute (layer, slot, source, destination, previousAmount);
        });
    }

    void showReconnectRouteMenu (int layer, int slot, int source, float amount, int targetStage)
    {
        if (! juce::isPositiveAndBelow (slot, VoiceParameters::modGraphSlotCount) || targetStage < 1 || targetStage > 3) return;
        juce::PopupMenu menu;
        menu.addSectionHeader ("RECONNECT ROUTE " + juce::String (slot + 1) + " / KEEP " + sourceName (source));
        for (const int destination : destinationsForStage (targetStage))
            menu.addItem (2000 + destination, destinationName (destination));
        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis, layer, slot, source, amount] (int result)
        {
            if (safeThis == nullptr || result < 2001 || result > 2000 + (int) ModDestination::wavefold) return;
            safeThis->setRoute (layer, slot, source, result - 2000, amount);
        });
    }

    void showNewRouteMenu (int layer, int targetStage)
    {
        if (targetStage < 1 || targetStage > 3) return;
        if (const int free = findFreeRouteSlot (layer); free >= 0)
        {
            showCreateRouteMenu (layer, free, targetStage);
            return;
        }

        juce::PopupMenu menu;
        menu.addSectionHeader ("ALL 4 ROUTE SLOTS ARE IN USE");
        for (int slot = 0; slot < VoiceParameters::modGraphSlotCount; ++slot)
            menu.addItem (5000 + slot, "REPLACE " + juce::String (slot + 1) + " / " + routeSummary (layer, slot));

        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis, layer, targetStage] (int result)
        {
            if (safeThis == nullptr || result < 5000 || result >= 5000 + VoiceParameters::modGraphSlotCount) return;
            safeThis->showCreateRouteMenu (layer, result - 5000, targetStage);
        });
    }

    void showEditRouteMenu (int layer, int slot)
    {
        const auto voice = voiceForLayer (layer);
        if (! juce::isPositiveAndBelow (slot, VoiceParameters::modGraphSlotCount)) return;
        const auto route = voice.moduleModSlots[(size_t) slot];
        if (route.source == (int) ModSource::none || route.destination == (int) ModDestination::none) return;

        juce::PopupMenu menu;
        menu.addSectionHeader ("EDIT ROUTE " + juce::String (slot + 1) + " / " + routeSummary (layer, slot));

        juce::PopupMenu sourceMenu;
        for (int source = (int) ModSource::lfo1; source <= (int) ModSource::lfo4; ++source)
            sourceMenu.addItem (1000 + source, sourceName (source), true, source == route.source);
        menu.addSubMenu ("SOURCE", sourceMenu);

        juce::PopupMenu destinationMenu;
        for (int stage = 1; stage <= 3; ++stage)
        {
            juce::PopupMenu group;
            for (const int destination : destinationsForStage (stage))
                group.addItem (2000 + destination, destinationName (destination), true, destination == route.destination);
            destinationMenu.addSubMenu (stageName (stage), group);
        }
        menu.addSubMenu ("DESTINATION", destinationMenu);

        static constexpr std::array<float, 9> depths {{ -1.0f, -0.75f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }};
        juce::PopupMenu depthMenu;
        for (size_t i = 0; i < depths.size(); ++i)
            depthMenu.addItem (3000 + (int) i, juce::String (depths[i], 2), true, std::abs (route.amount - depths[i]) < 0.015f);
        menu.addSubMenu ("DEPTH", depthMenu);
        menu.addSeparator();
        menu.addItem (9000, "REMOVE CONNECTION");

        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis, layer, slot, route] (int result)
        {
            if (safeThis == nullptr || result == 0) return;
            if (result >= 1001 && result <= 1000 + (int) ModSource::lfo4)
                safeThis->setRoute (layer, slot, result - 1000, route.destination, route.amount);
            else if (result >= 2001 && result <= 2000 + (int) ModDestination::wavefold)
                safeThis->setRoute (layer, slot, route.source, result - 2000, route.amount);
            else if (result >= 3000 && result < 3009)
            {
                static constexpr std::array<float, 9> values {{ -1.0f, -0.75f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }};
                safeThis->setRoute (layer, slot, route.source, route.destination, values[(size_t) (result - 3000)]);
            }
            else if (result == 9000)
                safeThis->clearRoute (layer, slot);
        });
    }


    std::vector<int> currentCombineLayerOrder() const
    {
        std::vector<std::pair<int, int>> sequenced;
        for (const auto& edge : edges)
            if (edge.editKind == EdgeEditKind::layerCombine && edge.layer >= 0)
                sequenced.emplace_back (edge.sequence >= 0 ? edge.sequence : edge.layer, edge.layer);
        std::sort (sequenced.begin(), sequenced.end(), [] (const auto& a, const auto& b)
        {
            return a.first == b.first ? a.second < b.second : a.first < b.first;
        });
        std::vector<int> result;
        result.reserve (sequenced.size());
        for (const auto& [sequence, layer] : sequenced) { juce::ignoreUnused (sequence); result.push_back (layer); }
        return result;
    }

    bool moveCombineLayer (int layer, int delta)
    {
        auto order = currentCombineLayerOrder();
        const auto it = std::find (order.begin(), order.end(), layer);
        if (it == order.end()) return false;
        const int index = (int) std::distance (order.begin(), it);
        const int target = index + delta;
        if (! juce::isPositiveAndBelow (target, (int) order.size())) return false;

        pushUndoState();
        std::swap (order[(size_t) index], order[(size_t) target]);
        const auto movedKey = "combine:" + juce::String (layer);
        juce::String reorderReason;
        if (! graphModel.reorderEdge (movedKey, target, &reorderReason))
            return false;

        for (const auto& modelEdge : graphModel.edges)
        {
            if (! modelEdge.id.startsWith ("combine:")) continue;
            restoredEdgeSequences[modelEdge.id.toStdString()] = modelEdge.sequence;
            for (auto& edge : edges)
                if (edge.key == modelEdge.id) edge.sequence = modelEdge.sequence;
        }
        stageGraphStateForPersistence();
        flushGraphStatePersistence();
        selectedEdgeKey = ("combine:" + juce::String (layer)).toStdString();
        repaint();
        return true;
    }

    void toggleNodeLock (const juce::String& nodeId)
    {
        const auto key = nodeId.toStdString();
        const bool locked = restoredNodeLocks.find (key) != restoredNodeLocks.end()
                         && restoredNodeLocks[key];
        juce::String lockReason;
        if (! graphModel.setNodeLocked (nodeId, ! locked, &lockReason))
        {
            graphValidationMessage = lockReason;
            repaint();
            return;
        }
        if (locked) restoredNodeLocks.erase (key);
        else restoredNodeLocks[key] = true;
        stageGraphStateForPersistence();
        repaint();
    }

    void showNodeLockMenu (const GraphNode& node)
    {
        const bool locked = restoredNodeLocks.find (node.id.toStdString()) != restoredNodeLocks.end();
        juce::PopupMenu menu;
        menu.addSectionHeader (node.title);
        menu.addItem (1, locked ? "UNLOCK POSITION" : "LOCK POSITION", true, locked);
        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        const auto id = node.id;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis, id] (int result)
        {
            if (safeThis != nullptr && result == 1) safeThis->toggleNodeLock (id);
        });
    }

    void showInstanceMenu (int layer)
    {
        if (layer >= 0 && ! proc.hasLayer (layer)) return;
        juce::PopupMenu menu;
        const bool selected = layer >= 0 && proc.getSoloLayer() == layer;
        const bool soloActive = proc.getSoloLayer() >= 0;
        menu.addSectionHeader (layer < 0 ? "MAIN INSTANCE" : "INSTANCE " + juce::String (layer + 2));
        menu.addItem (1, selected || (layer < 0 && soloActive) ? "CLEAR SOLO" : "SOLO THIS INSTANCE",
                      layer >= 0 || soloActive, selected);
        menu.addItem (2, "CLEAR ALL SOLO", true, soloActive);
        const auto nodeId = "L" + juce::String (layer) + ":S0";
        const bool locked = restoredNodeLocks.find (nodeId.toStdString()) != restoredNodeLocks.end();
        menu.addItem (3, locked ? "UNLOCK POSITION" : "LOCK POSITION");
        auto safeThis = juce::Component::SafePointer<SignalLabPage> (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis, layer, nodeId] (int result)
        {
            if (safeThis == nullptr) return;
            if (result == 1) safeThis->proc.setSoloLayer (layer < 0 || safeThis->proc.getSoloLayer() == layer ? -1 : layer);
            else if (result == 2) safeThis->proc.setSoloLayer (-1);
            else if (result == 3) safeThis->toggleNodeLock (nodeId);
            safeThis->repaint();
        });
    }

    void showCombineMenu (int layer)
    {
        if (layer < 0 || ! proc.hasLayer (layer)) return;
        const auto prefix = "layer" + juce::String (layer + 1);
        const int operation = juce::jlimit (0, 4, (int) parameter (prefix + "Operation", 0.0f));
        const float amount = parameter (prefix + "Amount", 1.0f);
        static const juce::StringArray operationNames { "ADD", "MIX", "SUBTRACT", "MULTIPLY", "DIVIDE" };
        static constexpr std::array<float, 5> amounts {{ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }};

        juce::PopupMenu menu;
        menu.addSectionHeader ("LAYER COMBINE CONNECTION / DSP TOPOLOGY STAYS VALID");
        juce::PopupMenu operationMenu;
        for (int i = 0; i < operationNames.size(); ++i)
            operationMenu.addItem (100 + i, operationNames[i], true, i == operation);
        menu.addSubMenu ("COMBINE MODE", operationMenu);

        juce::PopupMenu amountMenu;
        for (size_t i = 0; i < amounts.size(); ++i)
            amountMenu.addItem (200 + (int) i, juce::String (amounts[i], 2), true, std::abs (amount - amounts[i]) < 0.015f);
        menu.addSubMenu ("AMOUNT", amountMenu);

        const auto currentOrder = currentCombineLayerOrder();
        const auto orderIt = std::find (currentOrder.begin(), currentOrder.end(), layer);
        const int orderIndex = orderIt == currentOrder.end() ? -1 : (int) std::distance (currentOrder.begin(), orderIt);
        menu.addSeparator();
        menu.addItem (8001, "MOVE EARLIER", orderIndex > 0);
        menu.addItem (8002, "MOVE LATER", orderIndex >= 0 && orderIndex + 1 < (int) currentOrder.size());
        menu.addSeparator();
        menu.addItem (9000, "DISABLE LAYER / REMOVE FROM MIX");

        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis, layer, prefix] (int result)
        {
            if (safeThis == nullptr || result == 0) return;
            if (result == 8001) { safeThis->moveCombineLayer (layer, -1); return; }
            if (result == 8002) { safeThis->moveCombineLayer (layer, 1); return; }
            safeThis->pushUndoState();
            if (result >= 100 && result < 105)
                safeThis->setParameterValue (prefix + "Operation", (float) (result - 100));
            else if (result >= 200 && result < 205)
            {
                static constexpr std::array<float, 5> values {{ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }};
                safeThis->setParameterValue (prefix + "Amount", values[(size_t) (result - 200)]);
            }
            else if (result == 9000)
                safeThis->setParameterValue (prefix + "Enabled", 0.0f);
            safeThis->repaint();
        });
    }

    void setFxTopologyMode (const GraphNode& node, int mode)
    {
        if (node.role != NodeRole::stage || node.stage != 4) return;
        mode = juce::jlimit (0, 1, mode);
        pushUndoState();
        restoredNodeRoutingModes[node.id.toStdString()] = mode;
        if (auto* model = graphModel.findNode (node.id)) model->routingMode = mode;
        for (auto& live : nodes)
            if (live.id == node.id)
            {
                live.routingMode = mode;
                live.detail = mode == 1 ? "PARALLEL PRE || CORE > POST" : "SERIAL PRE > CORE > POST";
            }
        stageGraphStateForPersistence();
        flushGraphStatePersistence();
        selectedNodeId = node.id.toStdString();
        connectionValidationMessage = mode == 1 ? "FX topology: compensated PARALLEL PRE || CORE > POST"
                                                : "FX topology: canonical SERIAL PRE > CORE > POST";
        repaint();
    }

    void showFxTopologyMenu (const GraphNode& node)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("FX ROUTING / REAL DSP TOPOLOGY");
        menu.addItem (100, "SERIAL  PRE > BUILT-IN CORE > POST", true, node.routingMode == 0);
        menu.addItem (101, "PARALLEL  PRE || BUILT-IN CORE > POST  (50/50)", true, node.routingMode == 1);
        menu.addSeparator();
        menu.addItem (200, "Parallel split uses preallocated scratch + unity-correlated gain compensation", false, false);
        const bool locked = restoredNodeLocks.find (node.id.toStdString()) != restoredNodeLocks.end();
        menu.addItem (300, locked ? "UNLOCK POSITION" : "LOCK POSITION");
        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        const auto id = node.id;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis, id] (int result)
        {
            if (safeThis == nullptr) return;
            if (result == 300)
            {
                safeThis->toggleNodeLock (id);
                return;
            }
            if (result != 100 && result != 101) return;
            const int index = safeThis->findNodeIndex (id.toStdString());
            if (index >= 0) safeThis->setFxTopologyMode (safeThis->nodes[(size_t) index], result - 100);
        });
    }

    void showFixedConnectionMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("FIXED DSP SIGNAL PATH");
        menu.addItem (1, "This cable represents required processing order", false, false);
        menu.addItem (2, "Modulation and layer-combine cables are editable", false, false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [] (int) {});
    }

    void showEdgeMenu (const GraphEdge& edge)
    {
        if (edge.editKind == EdgeEditKind::modulationRoute) showEditRouteMenu (edge.layer, edge.slot);
        else if (edge.editKind == EdgeEditKind::layerCombine) showCombineMenu (edge.layer);
        else showFixedConnectionMenu();
    }

    void showModHubMenu (int layer)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("ADD MODULATION CONNECTION");
        menu.addItem (101, "TO OSC / WAVETABLE");
        menu.addItem (102, "TO 6-OP FM");
        menu.addItem (103, "TO FILTER / AMP");
        const auto nodeId = "L" + juce::String (layer) + ":MOD";
        const bool locked = restoredNodeLocks.find (nodeId.toStdString()) != restoredNodeLocks.end();
        menu.addItem (300, locked ? "UNLOCK POSITION" : "LOCK POSITION");
        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis, layer, nodeId] (int result)
        {
            if (safeThis == nullptr) return;
            if (result == 300) safeThis->toggleNodeLock (nodeId);
            else if (result >= 101 && result <= 103) safeThis->showNewRouteMenu (layer, result - 100);
        });
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

    int findNodeIndex (const std::string& id) const
    {
        if (id.empty()) return -1;
        for (int i = 0; i < (int) nodes.size(); ++i)
            if (nodes[(size_t) i].id.toStdString() == id) return i;
        return -1;
    }

    int hitTestNode (juce::Point<float> screenPoint) const
    {
        const auto world = toWorld (screenPoint);
        for (int i = (int) nodes.size() - 1; i >= 0; --i)
            if (nodes[(size_t) i].worldBounds.contains (world)) return i;
        return -1;
    }

    juce::Point<float> modOutputPoint (const GraphNode& node) const
    {
        const auto r = toScreen (node.worldBounds);
        return { r.getCentreX(), r.getY() };
    }

    juce::Point<float> modInputPoint (const GraphNode& node) const
    {
        const auto r = toScreen (node.worldBounds);
        return { r.getCentreX(), r.getBottom() };
    }

    int hitTestModOutput (juce::Point<float> screenPoint) const
    {
        const float radius = juce::jmax (8.0f, 8.0f * graphZoom);
        for (int i = (int) nodes.size() - 1; i >= 0; --i)
            if (nodes[(size_t) i].modOutputPort && modOutputPoint (nodes[(size_t) i]).getDistanceFrom (screenPoint) <= radius) return i;
        return -1;
    }

    int hitTestModInput (juce::Point<float> screenPoint) const
    {
        const float radius = juce::jmax (8.0f, 8.0f * graphZoom);
        for (int i = (int) nodes.size() - 1; i >= 0; --i)
            if (nodes[(size_t) i].modInputPort && modInputPoint (nodes[(size_t) i]).getDistanceFrom (screenPoint) <= radius) return i;
        return -1;
    }

    PatchGraph::ValidationResult validateModConnection (const GraphNode& from, const GraphNode& to) const
    {
        if (from.role != NodeRole::modHub || ! from.modOutputPort)
            return PatchGraph::ValidationResult::failure ("Source is not a modulation output");
        if (to.role != NodeRole::stage || ! to.modInputPort || to.stage < 1 || to.stage > 3)
            return PatchGraph::ValidationResult::failure ("Target is not a modulation-safe synthesis stage");
        if (from.layer != to.layer)
            return PatchGraph::ValidationResult::failure ("Modulation routes must stay inside the same synth instance");

        PatchGraph::Edge candidate;
        candidate.id = "preview:" + from.id + ">" + to.id;
        candidate.fromNode = from.id;
        candidate.fromPort = "mod.out";
        candidate.toNode = to.id;
        candidate.toPort = "mod.in";
        candidate.type = PatchGraph::PortType::modulation;
        candidate.editable = true;
        return graphModel.validateConnection (candidate);
    }

    bool canConnect (const GraphNode& from, const GraphNode& to) const
    {
        return validateModConnection (from, to).ok;
    }

    void stageGraphStateForPersistence()
    {
        // BIG view deliberately owns presentation-only node positions. Audio,
        // modulation and topology edits already commit through their parameters;
        // do not overwrite the compact map's saved geometry on every repaint.
        if (mapOnlyMode || graphModel.nodes.empty()) return;
        graphModel.view.pan = { graphPan.x, graphPan.y };
        graphModel.view.zoom = graphZoom;
        graphModel.view.snapToGrid = snapToGrid;
        for (const auto& node : nodes)
            if (graphModel.findNode (node.id) != nullptr)
            {
                juce::String reason;
                if (! graphModel.setNodePosition (node.id, { node.worldBounds.getX(), node.worldBounds.getY() }, &reason))
                    graphValidationMessage = reason;
            }

        const auto validation = graphModel.validate();
        graphValidationMessage = validation.ok ? juce::String {} : validation.message;
        if (! validation.ok) return;
        pendingGraphFingerprint = graphModel.fingerprint();
        graphStateDirty = pendingGraphFingerprint != lastPersistedGraphFingerprint;
    }

    void flushGraphStatePersistence()
    {
        if (! graphStateDirty) return;
        proc.setPatchGraphDocument (graphModel);
        lastPersistedGraphFingerprint = pendingGraphFingerprint;
        graphStateDirty = false;
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
            case ToolbarAction::undo: undoCableEdit(); return;
            case ToolbarAction::redo: redoCableEdit(); return;
            case ToolbarAction::autoArrange:
                pushUndoState();
                {
                    std::map<std::string, juce::Point<float>> lockedOffsets;
                    for (const auto& node : nodes)
                        if (node.locked)
                            lockedOffsets[node.id.toStdString()] = node.worldBounds.getPosition() - node.baseBounds.getPosition();
                nodeOffsets.clear();
                restoredNodePositions.clear();
                    for (auto& node : nodes)
                    {
                        const auto it = lockedOffsets.find (node.id.toStdString());
                        if (it != lockedOffsets.end())
                        {
                            nodeOffsets[node.id.toStdString()] = it->second;
                            node.worldBounds = node.baseBounds.translated (it->second.x, it->second.y);
                            restoredNodePositions[node.id.toStdString()] = node.worldBounds.getPosition();
                        }
                        else
                            node.worldBounds = node.baseBounds;
                    }
                }
                fitToView();
                break;
            case ToolbarAction::fit: fitToView(); break;
            case ToolbarAction::zoomOut: setZoomAround (graphViewport.getCentre(), graphZoom / 1.18f); break;
            case ToolbarAction::zoom100: setZoomAround (graphViewport.getCentre(), 1.0f); break;
            case ToolbarAction::zoomIn: setZoomAround (graphViewport.getCentre(), graphZoom * 1.18f); break;
            case ToolbarAction::grid: pushUndoState(); snapToGrid = ! snapToGrid; repaint(); break;
            case ToolbarAction::expand: showPatchMapOverlay(); break;
            case ToolbarAction::none: break;
        }
        stageGraphStateForPersistence();
    }

    void showPatchMapOverlay()
    {
        if (mapOnlyMode) return;

        // LaunchOptions sizes the top-level window around a fixed-size content component.
        // For the large map we need the opposite relationship: the SignalLabPage must follow
        // every dialog resize so its toolbar/viewport never live outside the visible window.
        auto* content = new SignalLabPage (proc, true);
        const float desktopScale = juce::Component::getApproximateScaleFactorForComponent (this);
        auto* dialogWindow = new LargePatchMapWindow (desktopScale);
        dialogWindow->setUsingNativeTitleBar (false);
        dialogWindow->setResizable (true, false);
        dialogWindow->setResizeLimits (900, 540, 2400, 1600);
        dialogWindow->setContentOwned (content, false);
        dialogWindow->centreWithSize (1320, 760);
        dialogWindow->setVisible (true);
        dialogWindow->enterModalState (true, nullptr, true);
    }

    void setZoomAround (juce::Point<float> screenPoint, float requestedZoom)
    {
        if (graphViewport.isEmpty()) return;
        const auto before = toWorld (screenPoint);
        graphZoom = juce::jlimit (kReadableGraphZoom, 2.2f, requestedZoom);
        graphPan = screenPoint - graphViewport.getPosition() - before * graphZoom;
        stageGraphStateForPersistence();
        repaint();
    }

    void applyFitToView()
    {
        if (nodes.empty() || graphViewport.isEmpty())
        {
            graphZoom = 0.82f;
            graphPan = { 18.0f, 18.0f };
            return;
        }
        auto world = nodes.front().worldBounds;
        for (size_t i = 1; i < nodes.size(); ++i) world = world.getUnion (nodes[i].worldBounds);
        world = world.expanded (28.0f);
        const float sx = graphViewport.getWidth() / juce::jmax (1.0f, world.getWidth());
        const float sy = graphViewport.getHeight() / juce::jmax (1.0f, world.getHeight());
        graphZoom = juce::jlimit (kReadableGraphZoom, 1.65f, juce::jmin (sx, sy));
        graphPan = graphViewport.getCentre() - graphViewport.getPosition() - world.getCentre() * graphZoom;
    }

    void fitToView()
    {
        applyFitToView();
        stageGraphStateForPersistence();
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
                 bool inputPort = true, bool outputPort = true, NodeRole role = NodeRole::stage,
                 int stage = -1, bool modInputPort = false, bool modOutputPort = false)
    {
        const auto stableId = id.toStdString();
        auto offset = juce::Point<float>();
        if (const auto it = nodeOffsets.find (stableId); it != nodeOffsets.end())
            offset = it->second;
        else if (const auto restored = restoredNodePositions.find (stableId); restored != restoredNodePositions.end())
        {
            offset = restored->second - base.getPosition();
            nodeOffsets[stableId] = offset;
        }

        GraphNode node;
        node.id = id; node.baseBounds = base; node.worldBounds = base.translated (offset.x, offset.y);
        node.title = title; node.detail = detail; node.tab = tab; node.layer = layer; node.colour = colour;
        node.inputPort = inputPort; node.outputPort = outputPort; node.role = role; node.stage = stage;
        node.modInputPort = modInputPort; node.modOutputPort = modOutputPort;
        if (const auto locked = restoredNodeLocks.find (stableId); locked != restoredNodeLocks.end())
            node.locked = locked->second;
        if (const auto routing = restoredNodeRoutingModes.find (stableId); routing != restoredNodeRoutingModes.end())
            node.routingMode = juce::jlimit (0, 1, routing->second);

        PatchGraph::Node modelNode;
        modelNode.id = id;
        modelNode.title = title;
        modelNode.position = { node.worldBounds.getX(), node.worldBounds.getY() };
        modelNode.positionValid = true;
        modelNode.locked = node.locked;
        modelNode.routingMode = node.routingMode;
        if (role == NodeRole::master) modelNode.type = PatchGraph::NodeType::master;
        else if (role == NodeRole::clock) modelNode.type = PatchGraph::NodeType::clock;
        else if (role == NodeRole::modHub) modelNode.type = PatchGraph::NodeType::modulationRouter;
        else if (stage == 0) modelNode.type = PatchGraph::NodeType::source;
        else if (stage == 5) modelNode.type = PatchGraph::NodeType::mixer;
        else modelNode.type = PatchGraph::NodeType::processor;
        const bool ownsAudioPorts = role != NodeRole::clock && role != NodeRole::modHub;
        if (inputPort && ownsAudioPorts) modelNode.ports.push_back (PatchGraph::audioInput (id == "GLOBALBUS"));
        if (outputPort && ownsAudioPorts) modelNode.ports.push_back (PatchGraph::audioOutput());
        if (modInputPort) modelNode.ports.push_back (PatchGraph::modulationInput());
        if (modOutputPort) modelNode.ports.push_back (PatchGraph::modulationOutput());
        if (role == NodeRole::modHub) modelNode.ports.push_back (PatchGraph::clockInput());
        if (role == NodeRole::clock) modelNode.ports.push_back (PatchGraph::clockOutput());
        juce::String reason;
        if (! graphModel.addNode (std::move (modelNode), &reason)) graphValidationMessage = reason;

        nodes.push_back (std::move (node));
        return (int) nodes.size() - 1;
    }

    void connect (int from, int to, EdgeKind kind = EdgeKind::audio, EdgeEditKind editKind = EdgeEditKind::none,
                  int layer = -2, int slot = -1, const juce::String& key = {}, const juce::String& label = {})
    {
        if (! juce::isPositiveAndBelow (from, (int) nodes.size()) || ! juce::isPositiveAndBelow (to, (int) nodes.size())) return;

        PatchGraph::Edge modelEdge;
        modelEdge.id = key;
        modelEdge.fromNode = nodes[(size_t) from].id;
        modelEdge.toNode = nodes[(size_t) to].id;
        modelEdge.editable = editKind != EdgeEditKind::none;
        int edgeSequence = -1;
        if (editKind == EdgeEditKind::layerCombine && key.isNotEmpty())
        {
            const auto stableKey = key.toStdString();
            const auto restored = restoredEdgeSequences.find (stableKey);
            edgeSequence = restored != restoredEdgeSequences.end() ? restored->second : juce::jmax (0, layer);
            restoredEdgeSequences[stableKey] = edgeSequence;
            modelEdge.sequence = edgeSequence;
        }
        if (kind == EdgeKind::audio)
        {
            modelEdge.fromPort = "audio.out"; modelEdge.toPort = "audio.in"; modelEdge.type = PatchGraph::PortType::audio;
        }
        else if (kind == EdgeKind::modulation)
        {
            modelEdge.fromPort = "mod.out"; modelEdge.toPort = "mod.in"; modelEdge.type = PatchGraph::PortType::modulation;
        }
        else
        {
            modelEdge.fromPort = "clock.out"; modelEdge.toPort = "clock.in"; modelEdge.type = PatchGraph::PortType::clock;
        }
        juce::String reason;
        if (! graphModel.addEdge (std::move (modelEdge), &reason))
        {
            graphValidationMessage = reason;
            return;
        }
        edges.push_back ({ from, to, kind, editKind, layer, slot, key, label, edgeSequence });
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
            const float sx = toScreen (juce::Point<float> { x, 0.0f }).x;
            g.drawVerticalLine ((int) sx, graphViewport.getY(), graphViewport.getBottom());
        }
        for (float y = y0; y <= bottomRight.y + step; y += step)
        {
            const float sy = toScreen (juce::Point<float> { 0.0f, y }).y;
            g.drawHorizontalLine ((int) sy, graphViewport.getX(), graphViewport.getRight());
        }
    }

    std::pair<juce::Point<float>, juce::Point<float>> edgeEndpoints (const GraphEdge& edge) const
    {
        const auto& from = nodes[(size_t) edge.from];
        const auto& to = nodes[(size_t) edge.to];
        if (edge.kind == EdgeKind::modulation) return { modOutputPoint (from), modInputPoint (to) };
        if (edge.kind == EdgeKind::clock)
        {
            const auto a = toScreen (from.worldBounds), b = toScreen (to.worldBounds);
            return { { a.getX(), a.getCentreY() }, { b.getCentreX(), b.getBottom() } };
        }
        const auto a = toScreen (from.worldBounds), b = toScreen (to.worldBounds);
        return { { a.getRight(), a.getCentreY() }, { b.getX(), b.getCentreY() } };
    }

    juce::Path edgePath (const GraphEdge& edge) const
    {
        juce::Path wire;
        if (! juce::isPositiveAndBelow (edge.from, (int) nodes.size()) || ! juce::isPositiveAndBelow (edge.to, (int) nodes.size())) return wire;
        const auto [a, b] = edgeEndpoints (edge);
        wire.startNewSubPath (a);
        if (edge.kind == EdgeKind::modulation)
        {
            const float lift = juce::jmax (18.0f, (22.0f + juce::jmax (0, edge.slot) * 5.0f) * graphZoom);
            wire.cubicTo ({ a.x, a.y - lift }, { b.x, b.y + lift }, b);
        }
        else
        {
            const float bend = juce::jmax (24.0f, std::abs (b.x - a.x) * 0.42f);
            wire.cubicTo ({ a.x + bend, a.y }, { b.x - bend, b.y }, b);
        }
        return wire;
    }

    int hitTestEdge (juce::Point<float> point) const
    {
        for (int i = (int) edges.size() - 1; i >= 0; --i)
        {
            if (! juce::isPositiveAndBelow (edges[(size_t) i].from, (int) nodes.size())
                || ! juce::isPositiveAndBelow (edges[(size_t) i].to, (int) nodes.size())) continue;
            juce::Path hitArea;
            juce::PathStrokeType (juce::jmax (14.0f, 16.0f * graphZoom)).createStrokedPath (hitArea, edgePath (edges[(size_t) i]));
            if (hitArea.contains (point.x, point.y)) return i;
        }
        return -1;
    }

    bool isReconnectDestinationHit (const GraphEdge& edge, juce::Point<float> point) const
    {
        if (edge.editKind != EdgeEditKind::modulationRoute) return false;
        const auto endpoints = edgeEndpoints (edge);
        return endpoints.second.getDistanceFrom (point) <= juce::jmax (11.0f, 13.0f * graphZoom);
    }

    bool deleteSelectedCable()
    {
        if (selectedEdgeKey.empty()) return false;
        for (const auto& edge : edges)
        {
            if (edge.key.toStdString() != selectedEdgeKey) continue;
            if (edge.editKind == EdgeEditKind::modulationRoute)
            {
                clearRoute (edge.layer, edge.slot);
                return true;
            }
            if (edge.editKind == EdgeEditKind::layerCombine && edge.layer >= 0 && proc.hasLayer (edge.layer))
            {
                pushUndoState();
                setParameterValue ("layer" + juce::String (edge.layer + 1) + "Enabled", 0.0f);
                selectedEdgeKey.clear();
                repaint();
                return true;
            }
            connectionValidationMessage = "Fixed audio/clock routing cannot be removed until the DSP compiler owns that topology";
            repaint();
            return false;
        }
        return false;
    }

    void drawEdgeLabel (juce::Graphics& g, const GraphEdge& edge, juce::Colour colour)
    {
        if (edge.label.isEmpty()) return;
        const auto [a, b] = edgeEndpoints (edge);
        const auto centre = (a + b) * 0.5f;
        const float width = juce::jlimit (54.0f, 150.0f, 12.0f + edge.label.length() * 5.1f * juce::jmax (0.75f, graphZoom));
        auto r = juce::Rectangle<float> (centre.x - width * 0.5f, centre.y - 8.0f, width, 16.0f);
        g.setColour (juce::Colour (0xe6070d0f)); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colour.withAlpha (0.85f)); g.drawRoundedRectangle (r, 3.0f, 0.8f);
        g.setColour (juce::Colour (0xffd5e2df));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 7.5f * graphZoom), juce::Font::bold)));
        g.drawFittedText (edge.label, r.reduced (4, 0).toNearestInt(), juce::Justification::centred, 1);
    }

    void drawEdge (juce::Graphics& g, const GraphEdge& edge, juce::Colour led, juce::Colour accent)
    {
        if (! juce::isPositiveAndBelow (edge.from, (int) nodes.size()) || ! juce::isPositiveAndBelow (edge.to, (int) nodes.size())) return;
        const auto& from = nodes[(size_t) edge.from];
        const auto& to = nodes[(size_t) edge.to];
        const auto wire = edgePath (edge);
        const bool selected = edge.key.isNotEmpty() && edge.key.toStdString() == selectedEdgeKey;

        if (edge.kind == EdgeKind::audio)
        {
            const auto colour = selected ? accent.brighter (0.2f) : from.colour.interpolatedWith (to.colour, 0.45f).withAlpha (0.7f);
            glow (g, wire, colour, juce::jmax (0.9f, graphZoom + (selected ? 0.8f : 0.0f)));
            if (edge.editKind == EdgeEditKind::layerCombine && (selected || graphZoom >= 0.62f)) drawEdgeLabel (g, edge, accent);
        }
        else if (edge.kind == EdgeKind::modulation)
        {
            const auto colour = selected ? juce::Colours::white : led;
            g.setColour (colour.withAlpha (selected ? 0.92f : 0.64f));
            g.strokePath (wire, juce::PathStrokeType (juce::jmax (1.0f, graphZoom * (selected ? 1.8f : 1.25f))));
            if (selected || graphZoom >= 0.62f) drawEdgeLabel (g, edge, led);
        }
        else
        {
            g.setColour (accent.withAlpha (0.32f));
            g.strokePath (wire, juce::PathStrokeType (juce::jmax (0.7f, graphZoom * 0.85f)));
        }
    }

    void drawNode (juce::Graphics& g, const GraphNode& node, juce::Colour led, juce::Colour accent)
    {
        auto r = toScreen (node.worldBounds);
        const auto nodeId = node.id.toStdString();
        const bool selected = nodeId == selectedNodeId || selectedNodeIds.count (nodeId) != 0;
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (r.translated (0, 2.5f * graphZoom), 5.0f * graphZoom);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17272b), r.getTopLeft(), juce::Colour (0xff0d171a), r.getBottomRight(), false));
        g.fillRoundedRectangle (r, 5.0f * graphZoom);
        g.setColour ((selected ? juce::Colours::white : node.colour).withAlpha (selected ? 0.95f : 0.72f));
        g.drawRoundedRectangle (r, 5.0f * graphZoom, juce::jmax (1.0f, 1.3f * graphZoom));
        const bool showDetail = graphZoom >= 0.62f && r.getWidth() >= 72.0f && r.getHeight() >= 27.0f;
        auto textBounds = r.reduced (juce::jmax (4.0f, 6.0f * graphZoom), juce::jmax (1.0f, 2.0f * graphZoom));
        g.setColour (node.colour);
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.2f, 10.0f * graphZoom), juce::Font::bold)));
        if (showDetail)
        {
            g.drawFittedText (node.title, textBounds.removeFromTop (textBounds.getHeight() * 0.54f).toNearestInt(), juce::Justification::centredLeft, 1);
            g.setColour (accent.withAlpha (0.82f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.4f, 8.5f * graphZoom))));
            g.drawFittedText (node.detail, textBounds.toNearestInt(), juce::Justification::centredLeft, 1);
        }
        else
        {
            g.drawFittedText (node.title, textBounds.toNearestInt(), juce::Justification::centredLeft, 1);
        }

        if (node.role == NodeRole::stage && node.stage == 0)
        {
            const float peak = node.layer < 0 ? routingMeters.mainPeak
                                              : routingMeters.layerPeak[(size_t) juce::jlimit (0, VoiceParameters::extraLayerCount - 1, node.layer)];
            const float meterWidth = juce::jmax (0.0f, r.getWidth() - 8.0f * graphZoom);
            const float level = juce::jlimit (0.0f, 1.0f, peak);
            g.setColour (juce::Colour (0xff071013));
            g.fillRoundedRectangle (r.getX() + 4.0f * graphZoom, r.getBottom() - 3.0f * graphZoom,
                                    meterWidth, juce::jmax (1.0f, 2.0f * graphZoom), 1.0f * graphZoom);
            g.setColour ((level > 0.9f ? juce::Colour (0xffff9673) : node.colour).withAlpha (0.9f));
            g.fillRoundedRectangle (r.getX() + 4.0f * graphZoom, r.getBottom() - 3.0f * graphZoom,
                                    meterWidth * level, juce::jmax (1.0f, 2.0f * graphZoom), 1.0f * graphZoom);
        }

        const float portRadius = juce::jmax (3.0f, 3.1f * graphZoom);
        g.setColour (node.colour.withAlpha (0.9f));
        if (node.inputPort) g.fillEllipse (r.getX() - portRadius, r.getCentreY() - portRadius, portRadius * 2, portRadius * 2);
        if (node.outputPort) g.fillEllipse (r.getRight() - portRadius, r.getCentreY() - portRadius, portRadius * 2, portRadius * 2);

        if (node.modInputPort)
        {
            const auto p = modInputPoint (node);
            bool valid = false;
            if (connecting)
            {
                const int from = findNodeIndex (connectionFromId);
                valid = from >= 0 && canConnect (nodes[(size_t) from], node);
                if (valid)
                {
                    const bool hover = node.id.toStdString() == hoverTargetId;
                    g.setColour (led.withAlpha (hover ? 0.28f : 0.12f));
                    g.fillEllipse (p.x - portRadius * 3.0f, p.y - portRadius * 3.0f, portRadius * 6.0f, portRadius * 6.0f);
                }
            }
            g.setColour ((valid ? led : accent).withAlpha (valid ? 1.0f : 0.72f));
            g.fillEllipse (p.x - portRadius, p.y - portRadius, portRadius * 2.0f, portRadius * 2.0f);
        }

        if (node.modOutputPort)
        {
            const auto p = modOutputPoint (node);
            g.setColour (led.withAlpha (0.18f));
            g.fillEllipse (p.x - portRadius * 2.3f, p.y - portRadius * 2.3f, portRadius * 4.6f, portRadius * 4.6f);
            g.setColour (led);
            g.fillEllipse (p.x - portRadius, p.y - portRadius, portRadius * 2.0f, portRadius * 2.0f);
        }

        if (node.locked)
        {
            g.setColour (juce::Colour (0xffffbd65).withAlpha (0.9f));
            g.drawRect (r.getRight() - 11.0f * graphZoom, r.getY() + 5.0f * graphZoom,
                        6.0f * graphZoom, 5.0f * graphZoom, juce::jmax (1.0f, graphZoom));
        }

        if (node.role == NodeRole::clock)
        {
            g.setColour (accent.withAlpha (0.8f));
            g.fillEllipse (r.getX() - portRadius, r.getCentreY() - portRadius, portRadius * 2.0f, portRadius * 2.0f);
        }
        else if (node.role == NodeRole::modHub)
        {
            g.setColour (accent.withAlpha (0.65f));
            g.fillEllipse (r.getCentreX() - portRadius, r.getBottom() - portRadius, portRadius * 2.0f, portRadius * 2.0f);
        }
    }

    void drawConnectionPreview (juce::Graphics& g, juce::Colour led, juce::Colour accent)
    {
        if (! connecting) return;
        const int from = findNodeIndex (connectionFromId);
        if (from < 0) return;
        const auto start = modOutputPoint (nodes[(size_t) from]);
        auto end = connectionDragPoint;
        bool valid = false;
        if (const int target = findNodeIndex (hoverTargetId); target >= 0)
        {
            valid = canConnect (nodes[(size_t) from], nodes[(size_t) target]);
            if (valid) end = modInputPoint (nodes[(size_t) target]);
        }
        juce::Path preview;
        preview.startNewSubPath (start);
        const float lift = juce::jmax (22.0f, std::abs (end.y - start.y) * 0.32f);
        preview.cubicTo ({ start.x, start.y - lift }, { end.x, end.y + lift }, end);
        const auto colour = valid ? led : accent.withSaturation (0.25f);
        glow (g, preview, colour.withAlpha (valid ? 0.88f : 0.48f), juce::jmax (1.0f, graphZoom * 1.3f));
    }

    void drawToolbar (juce::Graphics& g, juce::Rectangle<float> header, juce::Colour led, juce::Colour accent)
    {
        toolbarButtons.clear();
        const bool compactHeader = mapOnlyMode || header.getHeight() > 40.0f;
        const bool veryNarrow = header.getWidth() < 620.0f;
        const float gap = veryNarrow ? 4.0f : 6.0f;
        // 28 px is the minimum practical toolbar hit target at JUCE logical scale.
        const float h = mapOnlyMode ? 30.0f : (compactHeader ? 26.0f : 24.0f);
        struct Def { const char* label; float width; ToolbarAction action; };
        const Def defs[] {{ "BIG", 42, ToolbarAction::expand }, { "UNDO", 48, ToolbarAction::undo }, { "REDO", 48, ToolbarAction::redo },
                          { "AUTO", 48, ToolbarAction::autoArrange }, { "FIT", 40, ToolbarAction::fit }, { "-", 26, ToolbarAction::zoomOut },
                          { "100%", 42, ToolbarAction::zoom100 }, { "+", 26, ToolbarAction::zoomIn }, { "GRID", 48, ToolbarAction::grid }};

        float total = -gap;
        for (const auto& d : defs)
            if (! (mapOnlyMode && d.action == ToolbarAction::expand)) total += (veryNarrow ? juce::jmax (24.0f, d.width - 6.0f) : d.width) + gap;

        auto buttonRow = compactHeader ? header.removeFromBottom (mapOnlyMode ? 36.0f : 30.0f) : header;
        float x = compactHeader ? buttonRow.getX() + 6.0f : buttonRow.getRight() - total - 6.0f;
        for (const auto& d : defs)
        {
            if (mapOnlyMode && d.action == ToolbarAction::expand) continue;
            const float buttonWidth = veryNarrow ? juce::jmax (24.0f, d.width - 6.0f) : d.width;
            juce::Rectangle<float> r (x, buttonRow.getCentreY() - h * 0.5f, buttonWidth, h);
            toolbarButtons.push_back ({ r, d.label, d.action });
            const bool active = d.action == ToolbarAction::grid && snapToGrid;
            g.setColour (active ? led.withAlpha (0.18f) : juce::Colour (0xff132126)); g.fillRoundedRectangle (r, 4);
            g.setColour ((active ? led : accent).withAlpha (0.8f)); g.drawRoundedRectangle (r, 4, 1);
            g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold))); g.setColour (active ? led : juce::Colour (0xffb7c5c8));
            g.drawText (d.label, r, juce::Justification::centred);
            x += buttonWidth + gap;
        }

        auto textArea = compactHeader ? header.reduced (8.0f, 0.0f)
                                      : header.withTrimmedRight (total + 12.0f).reduced (8.0f, 0.0f);
        g.setFont (juce::Font (juce::FontOptions (compactHeader ? 9.5f : 9.0f, juce::Font::bold)));
        const bool daw = parameter ("tempoSource", 1.0f) >= 0.5f;
        juce::String status = compactHeader
            ? "PATCH MAP / RIGHT-CLICK FX = SERIAL/PARALLEL / DRAG MOD = ADD / DEL = REMOVE     CLOCK "
                + juce::String (proc.getEffectiveBpm(), 1) + " " + (daw ? "DAW" : "MANUAL")
            : "PATCH MAP / RIGHT-CLICK FX = SERIAL/PARALLEL / DRAG MOD = ADD / DEL = REMOVE / CTRL-CMD+Z = UNDO    CLOCK: "
                + juce::String (proc.getEffectiveBpm(), 1) + " BPM " + (daw ? "DAW" : "MANUAL");
        auto statusColour = led;
        if (connectionValidationMessage.isNotEmpty())
        {
            status = "ROUTE REJECTED / " + connectionValidationMessage;
            statusColour = juce::Colour (0xffff9673);
        }
        else if (graphValidationMessage.isNotEmpty())
        {
            status = "GRAPH VALIDATION / " + graphValidationMessage;
            statusColour = juce::Colour (0xffff9673);
        }
        g.setColour (statusColour);
        g.drawFittedText (status, textArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void drawPatchMap (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour led, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xff03080b)); g.fillRoundedRectangle (bounds, 7);
        g.setColour (juce::Colour (0xff506166)); g.drawRoundedRectangle (bounds, 7, 1);
        const float headerHeight = mapOnlyMode ? 72.0f : (bounds.getWidth() < 980.0f ? 56.0f : 30.0f);
        auto header = bounds.removeFromTop (headerHeight);
        graphViewport = bounds.reduced (4);

        nodes.clear(); edges.clear();
        graphModel.clearTopology();
        graphValidationMessage.clear();
        std::vector<int> instances { -1 };
        for (int i = 0; i < VoiceParameters::extraLayerCount; ++i)
            if (proc.hasLayer (i) && parameter ("layer" + juce::String (i + 1) + "Enabled") >= 0.5f) instances.push_back (i);

        const float rowGap = 112.0f, nodeW = 104.0f, nodeH = 42.0f;
        const std::array<float, 6> xs {{ 0, 122, 244, 366, 488, 610 }};
        const juce::String stages[] { "INSTANCE", "OSC / WT", "6-OP FM", "FILTER / AMP", "FX", "COMBINE" };
        const juce::String tabs[] { "SYNTH", "SYNTH", "FM", "FILTER", "FX", "LAYERS" };
        const juce::uint32 colours[] { 0xff54f5d1, 0xffffbd65, 0xffc9a0ff, 0xff78f1c4, 0xffff91b8, 0xffa6cf75, 0xff94aaff, 0xffff9673 };
        std::vector<int> combineNodes, combineLayers, modNodes;
        routingMeters = proc.getRoutingMeters();

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
            const auto fxKey = (prefix + ":S4").toStdString();
            const auto fxModeIt = restoredNodeRoutingModes.find (fxKey);
            const int fxMode = fxModeIt != restoredNodeRoutingModes.end() ? juce::jlimit (0, 1, fxModeIt->second) : 0;
            const juce::String fxDetail = fxMode == 1 ? "PARALLEL PRE || CORE > POST" : "SERIAL PRE > CORE > POST";
            const juce::String details[] { instanceDetail, "sources + tables", "algorithm + ops", "cutoff + ADSR", fxDetail, combineDetail };
            std::array<int, 6> rowNodes {};
            for (int stage = 0; stage < 6; ++stage)
            {
                rowNodes[(size_t) stage] = addNode ({ xs[(size_t) stage], y, nodeW, nodeH }, prefix + ":S" + juce::String (stage),
                    stage == 0 ? (layer < 0 ? "INSTANCE 1 / MAIN" : "INSTANCE " + juce::String (layer + 2)) : stages[stage],
                    details[stage], tabs[stage], layer, colour, stage != 0, true, NodeRole::stage, stage,
                    stage >= 1 && stage <= 3, false);
                if (stage > 0) connect (rowNodes[(size_t) stage - 1], rowNodes[(size_t) stage], EdgeKind::audio);
            }
            combineNodes.push_back (rowNodes.back());
            combineLayers.push_back (layer);

            const auto voice = voiceForLayer (layer);
            int activeRoutes = 0;
            for (const auto& route : voice.moduleModSlots)
                if (route.source != (int) ModSource::none && route.destination != (int) ModDestination::none) ++activeRoutes;

            const int mod = addNode ({ 305.0f, y + 58.0f, 126.0f, 36.0f }, prefix + ":MOD", "MOD / ROUTES",
                                     juce::String (activeRoutes) + " ACTIVE / DRAG JACK", "MOD", layer, accent,
                                     false, false, NodeRole::modHub, -1, false, true);
            modNodes.push_back (mod);

            for (int slot = 0; slot < VoiceParameters::modGraphSlotCount; ++slot)
            {
                const auto& route = voice.moduleModSlots[(size_t) slot];
                const int targetStage = stageForDestination (route.destination);
                if (route.source == (int) ModSource::none || route.destination == (int) ModDestination::none
                    || targetStage < 1 || targetStage > 3) continue;
                const auto key = "route:" + juce::String (layer) + ":" + juce::String (slot);
                const auto label = sourceName (route.source) + " > " + destinationName (route.destination) + " " + juce::String (route.amount, 2);
                connect (mod, rowNodes[(size_t) targetStage], EdgeKind::modulation, EdgeEditKind::modulationRoute,
                         layer, slot, key, label);
            }
        }

        const float firstY = nodeH * 0.5f;
        const float lastY = (instances.size() - 1) * rowGap + nodeH * 0.5f;
        const float masterY = (firstY + lastY) * 0.5f;
        int activeGlobalFx = 0;
        for (int i = 1; i <= FxModuleParameters::slotCount; ++i)
            if ((int) parameter ("globalFxModule" + juce::String (i) + "Type", 0.0f) > 0
                && parameter ("globalFxModule" + juce::String (i) + "Bypass", 0.0f) < 0.5f) ++activeGlobalFx;
        const int globalBus = addNode ({ 760.0f, masterY - nodeH * 0.5f, 132.0f, nodeH }, "GLOBALBUS", "GLOBAL FILTER / FX",
                                       juce::String (activeGlobalFx) + " ACTIVE / WHOLE MIX", "FX", -2, accent, true, true, NodeRole::stage);
        const int master = addNode ({ 930.0f, masterY - nodeH * 0.5f, 126.0f, nodeH }, "MASTER", "MASTER OUT",
                                    juce::String (parameter ("masterOutputGain", 0.0f), 1) + " dB", "FX", -2, led,
                                    true, false, NodeRole::master);
        for (size_t i = 0; i < combineNodes.size(); ++i)
        {
            const int layer = combineLayers[i];
            if (layer < 0)
                connect (combineNodes[i], globalBus, EdgeKind::audio);
            else
            {
                const auto prefix = "layer" + juce::String (layer + 1);
                const int operation = juce::jlimit (0, 4, (int) parameter (prefix + "Operation", 0.0f));
                const juce::String opNames[] { "ADD", "MIX", "SUB", "MULT", "DIV" };
                const auto combineKey = "combine:" + juce::String (layer);
                const auto restoredSequence = restoredEdgeSequences.find (combineKey.toStdString());
                const int displaySequence = restoredSequence != restoredEdgeSequences.end() ? restoredSequence->second : layer;
                connect (combineNodes[i], globalBus, EdgeKind::audio, EdgeEditKind::layerCombine, layer, -1,
                         combineKey, "ORDER " + juce::String (displaySequence + 1) + " / EDIT / " + opNames[operation] + " "
                                     + juce::String (parameter (prefix + "Amount", 1.0f), 2));
            }
        }
        connect (globalBus, master, EdgeKind::audio);

        const int clock = addNode ({ 930.0f, masterY + 64.0f, 126.0f, nodeH }, "CLOCK", "TEMPO CLOCK",
                                   juce::String (proc.getEffectiveBpm(), 1) + " BPM", "MOD", -2, accent,
                                   false, true, NodeRole::clock);
        for (const auto mod : modNodes) connect (clock, mod, EdgeKind::clock);

        if (mapOnlyMode && bigViewNeedsFit)
        {
            applyFitToView();
            bigViewNeedsFit = false;
        }

        drawToolbar (g, header, led, accent);
        g.saveState(); g.reduceClipRegion (graphViewport.toNearestInt());
        drawGrid (g, led);
        for (const auto& edge : edges) drawEdge (g, edge, led, accent);
        for (const auto& node : nodes) drawNode (g, node, led, accent);
        drawConnectionPreview (g, led, accent);
        g.restoreState();
        stageGraphStateForPersistence();
    }

    void timerCallback() override
    {
        flushGraphStatePersistence();
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
