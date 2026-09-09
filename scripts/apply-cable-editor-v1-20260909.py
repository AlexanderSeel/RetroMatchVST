from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {old[:90]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


patch = ROOT / "Source/Engine/PatchGraph.h"
signal = ROOT / "Source/UI/SignalLabPage.h"
tests = ROOT / "Tests/SmokeTests.cpp"
static = ROOT / "scripts/static-check.py"
plan = ROOT / "plan.md"

# -----------------------------------------------------------------------------
# PatchGraph: safe mutable-edge primitives. Audio edits must leave the complete
# graph valid, so fixed/required routes cannot accidentally be removed.
# -----------------------------------------------------------------------------
replace_once(
    patch,
'''    Node* findNode (const juce::String& nodeId) noexcept
    {
        for (auto& node : nodes)
            if (node.id == nodeId) return &node;
        return nullptr;
    }

    bool addNode (Node node, juce::String* reason = nullptr)
''',
'''    Node* findNode (const juce::String& nodeId) noexcept
    {
        for (auto& node : nodes)
            if (node.id == nodeId) return &node;
        return nullptr;
    }

    const Edge* findEdge (const juce::String& edgeId) const noexcept
    {
        for (const auto& edge : edges)
            if (edge.id == edgeId) return &edge;
        return nullptr;
    }

    Edge* findEdge (const juce::String& edgeId) noexcept
    {
        for (auto& edge : edges)
            if (edge.id == edgeId) return &edge;
        return nullptr;
    }

    bool addNode (Node node, juce::String* reason = nullptr)
''')

replace_once(
    patch,
'''        edges.push_back (std::move (edge));
        return true;
    }

    ValidationResult validate() const
''',
'''        edges.push_back (std::move (edge));
        return true;
    }

    bool removeEdge (const juce::String& edgeId, juce::String* reason = nullptr, bool requireValidGraph = true)
    {
        Document candidate = *this;
        const auto it = std::find_if (candidate.edges.begin(), candidate.edges.end(), [&] (const Edge& edge) { return edge.id == edgeId; });
        if (it == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Connection not found: " + edgeId;
            return false;
        }
        candidate.edges.erase (it);
        if (requireValidGraph)
        {
            const auto validation = candidate.validate();
            if (! validation.ok)
            {
                if (reason != nullptr) *reason = validation.message;
                return false;
            }
        }
        *this = std::move (candidate);
        return true;
    }

    bool replaceEdge (const juce::String& edgeId, Edge replacement, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        const auto it = std::find_if (candidate.edges.begin(), candidate.edges.end(), [&] (const Edge& edge) { return edge.id == edgeId; });
        if (it == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Connection not found: " + edgeId;
            return false;
        }
        candidate.edges.erase (it);
        if (replacement.id.isEmpty()) replacement.id = edgeId;
        juce::String localReason;
        if (! candidate.addEdge (std::move (replacement), &localReason))
        {
            if (reason != nullptr) *reason = localReason;
            return false;
        }
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    ValidationResult validate() const
''')

# -----------------------------------------------------------------------------
# SignalLab: keyboard actions, cable selection/reconnect, local undo/redo history
# that restores both persisted graph layout and the DSP-backed route parameters.
# -----------------------------------------------------------------------------
replace_once(
    signal,
'''    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! graphViewport.contains (e.position)) return;
        setZoomAround (e.position, graphZoom * (1.0f + wheel.deltaY * 0.18f));
    }

private:
''',
'''    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
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
''')

replace_once(
    signal,
'''    enum class EdgeKind { audio, modulation, clock };
    enum class EdgeEditKind { none, modulationRoute, layerCombine };
    enum class NodeRole { stage, modHub, master, clock };
    enum class ToolbarAction { none, autoArrange, fit, zoomOut, zoom100, zoomIn, grid, expand };
''',
'''    enum class EdgeKind { audio, modulation, clock };
    enum class EdgeEditKind { none, modulationRoute, layerCombine };
    enum class NodeRole { stage, modHub, master, clock };
    enum class ToolbarAction { none, undo, redo, autoArrange, fit, zoomOut, zoom100, zoomIn, grid, expand };
''')

replace_once(
    signal,
'''    struct ToolbarButton { juce::Rectangle<float> bounds; juce::String label; ToolbarAction action = ToolbarAction::none; };

    RetroMatchSynthAudioProcessor& proc;
''',
'''    struct ToolbarButton { juce::Rectangle<float> bounds; juce::String label; ToolbarAction action = ToolbarAction::none; };

    struct CableHistoryState
    {
        PatchGraph::Document graph;
        std::array<std::array<ModSlotParameters, VoiceParameters::modGraphSlotCount>, VoiceParameters::extraLayerCount + 1> routes {};
        std::array<bool, VoiceParameters::extraLayerCount> layerPresent {};
        std::array<float, VoiceParameters::extraLayerCount> layerEnabled {}, layerOperation {}, layerAmount {};
        int editingLayer = -1;
    };

    RetroMatchSynthAudioProcessor& proc;
''')

replace_once(
    signal,
'''    bool panning = false, draggingNode = false, snapToGrid = true, connecting = false;
    juce::Point<float> panDragStart, panAtDragStart, nodeDragStartWorld, nodeOffsetAtDragStart, connectionDragPoint;
    std::string selectedNodeId, draggingNodeId, selectedEdgeKey, connectionFromId, hoverTargetId;
''',
'''    bool panning = false, draggingNode = false, snapToGrid = true, connecting = false;
    bool restoringHistory = false, dragHistoryCaptured = false;
    juce::Point<float> panDragStart, panAtDragStart, nodeDragStartWorld, nodeOffsetAtDragStart, connectionDragPoint;
    std::string selectedNodeId, draggingNodeId, selectedEdgeKey, connectionFromId, hoverTargetId;
    int reconnectLayer = -2, reconnectSlot = -1, reconnectSource = (int) ModSource::none;
    float reconnectAmount = 0.5f;
    std::vector<CableHistoryState> undoHistory, redoHistory;
''')

replace_once(
    signal,
'''    static juce::String stageName (int stage)
    {
        if (stage == 1) return "OSC / WT";
        if (stage == 2) return "6-OP FM";
        if (stage == 3) return "FILTER / AMP";
        return "SUPPORTED DESTINATION";
    }

    void setRoute (int layer, int slot, int source, int destination, float amount)
    {
        if (! juce::isPositiveAndBelow (slot, VoiceParameters::modGraphSlotCount)) return;
        if (layer >= 0 && ! proc.hasLayer (layer)) return;

        proc.selectEditingLayer (layer);
        const auto prefix = "moduleMod" + juce::String (slot + 1);
        setParameterValue (prefix + "Source", (float) source);
        setParameterValue (prefix + "Dest", (float) destination);
        setParameterValue (prefix + "Amount", juce::jlimit (-1.0f, 1.0f, amount));
        if (layer >= 0) proc.refreshEditingLayer();
        selectedEdgeKey = ("route:" + juce::String (layer) + ":" + juce::String (slot)).toStdString();
        repaint();
    }

    void clearRoute (int layer, int slot)
    {
        setRoute (layer, slot, (int) ModSource::none, (int) ModDestination::none, 0.0f);
        selectedEdgeKey.clear();
    }
''',
'''    static juce::String stageName (int stage)
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
        restoredNodePositions.clear(); nodeOffsets.clear();
        for (const auto& node : state.graph.nodes)
            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };
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
''')

# Plain click selects. Clicking the destination jack of an existing modulation
# cable starts endpoint reconnect. Double-click/right-click still open the menu.
replace_once(
    signal,
'''        if (! e.mods.isMiddleButtonDown())
        {
            // Editable cables are deliberately left-clickable as well as right-clickable.
            // This removes the hidden-context-menu feel of the first patch-map version.
            if (const int edge = hitTestEdge (e.position); edge >= 0 && edges[(size_t) edge].editKind != EdgeEditKind::none)
            {
                selectedEdgeKey = edges[(size_t) edge].key.toStdString();
                showEdgeMenu (edges[(size_t) edge]);
                repaint();
                return;
            }
            if (const int output = hitTestModOutput (e.position); output >= 0)
            {
                connectionValidationMessage.clear();
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
''',
'''        if (! e.mods.isMiddleButtonDown())
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
''')

replace_once(
    signal,
'''        if (draggingNode)
        {
            auto offset = nodeOffsetAtDragStart + (toWorld (e.position) - nodeDragStartWorld);
''',
'''        if (draggingNode)
        {
            if (! dragHistoryCaptured)
            {
                pushUndoState();
                dragHistoryCaptured = true;
            }
            auto offset = nodeOffsetAtDragStart + (toWorld (e.position) - nodeDragStartWorld);
''')

replace_once(
    signal,
'''        if (connecting)
        {
            const auto fromId = connectionFromId;
            const auto targetId = hoverTargetId;
            connecting = false;
            connectionFromId.clear();
            hoverTargetId.clear();

            const int from = findNodeIndex (fromId);
            const int target = findNodeIndex (targetId);
            if (from >= 0 && target >= 0)
            {
                const auto validation = validateModConnection (nodes[(size_t) from], nodes[(size_t) target]);
                if (validation.ok)
                {
                    connectionValidationMessage.clear();
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
        stageGraphStateForPersistence();
''',
'''        if (connecting)
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
        dragHistoryCaptured = false;
        stageGraphStateForPersistence();
''')

# Endpoint reconnect menu keeps the original source/depth and changes the target.
replace_once(
    signal,
'''    void showNewRouteMenu (int layer, int targetStage)
    {
''',
'''    void showReconnectRouteMenu (int layer, int slot, int source, float amount, int targetStage)
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
''')

# Layer-combine mutations are part of the same undo domain.
replace_once(
    signal,
'''        {
            if (safeThis == nullptr || result == 0) return;
            if (result >= 100 && result < 105)
                safeThis->setParameterValue (prefix + "Operation", (float) (result - 100));
''',
'''        {
            if (safeThis == nullptr || result == 0) return;
            safeThis->pushUndoState();
            if (result >= 100 && result < 105)
                safeThis->setParameterValue (prefix + "Operation", (float) (result - 100));
''')

# Safe selected-cable deletion + endpoint hit test.
replace_once(
    signal,
'''    int hitTestEdge (juce::Point<float> point) const
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

    void drawEdgeLabel
''',
'''    int hitTestEdge (juce::Point<float> point) const
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

    void drawEdgeLabel
''')

# Toolbar undo/redo and history around destructive layout actions.
replace_once(
    signal,
'''        switch (action)
        {
            case ToolbarAction::autoArrange:
                nodeOffsets.clear();
''',
'''        switch (action)
        {
            case ToolbarAction::undo: undoCableEdit(); return;
            case ToolbarAction::redo: redoCableEdit(); return;
            case ToolbarAction::autoArrange:
                pushUndoState();
                nodeOffsets.clear();
''')

replace_once(
    signal,
'''            case ToolbarAction::grid: snapToGrid = ! snapToGrid; repaint(); break;
''',
'''            case ToolbarAction::grid: pushUndoState(); snapToGrid = ! snapToGrid; repaint(); break;
''')

replace_once(
    signal,
'''        const Def defs[] {{ "BIG", 42, ToolbarAction::expand }, { "AUTO", 48, ToolbarAction::autoArrange }, { "FIT", 40, ToolbarAction::fit },
                          { "-", 26, ToolbarAction::zoomOut }, { "100%", 42, ToolbarAction::zoom100 },
                          { "+", 26, ToolbarAction::zoomIn }, { "GRID", 48, ToolbarAction::grid }};
''',
'''        const Def defs[] {{ "BIG", 42, ToolbarAction::expand }, { "UNDO", 48, ToolbarAction::undo }, { "REDO", 48, ToolbarAction::redo },
                          { "AUTO", 48, ToolbarAction::autoArrange }, { "FIT", 40, ToolbarAction::fit }, { "-", 26, ToolbarAction::zoomOut },
                          { "100%", 42, ToolbarAction::zoom100 }, { "+", 26, ToolbarAction::zoomIn }, { "GRID", 48, ToolbarAction::grid }};
''')

replace_once(
    signal,
'''        juce::String status = "PATCH MAP / TYPED + PERSISTENT GRAPH / DRAG MOD JACK TO ADD / BIG = LARGE OVERLAY    CLOCK: "
''',
'''        juce::String status = "PATCH MAP / DRAG MOD JACK = ADD / DRAG CABLE END = RECONNECT / DEL = REMOVE / CTRL-CMD+Z = UNDO    CLOCK: "
''')

# -----------------------------------------------------------------------------
# Regression tests for safe remove/replace operations.
# -----------------------------------------------------------------------------
replace_once(
    tests,
'''        PatchGraph::Edge wrong = a; wrong.id = "wrong"; wrong.toPort = "mod.in";
        if (graph.validateConnection (wrong).ok) return fail ("patch graph allowed incompatible port types");

        PatchGraph::Document cycle;
''',
'''        PatchGraph::Edge wrong = a; wrong.id = "wrong"; wrong.toPort = "mod.in";
        if (graph.validateConnection (wrong).ok) return fail ("patch graph allowed incompatible port types");
        juce::String mutationReason;
        if (graph.removeEdge ("b", &mutationReason)) return fail ("patch graph removed a required audio route that orphaned MASTER");
        if (graph.findEdge ("b") == nullptr) return fail ("failed graph mutation changed the original document");

        PatchGraph::Document editable;
        PatchGraph::Node mod; mod.id = "MOD"; mod.type = PatchGraph::NodeType::modulationRouter; mod.ports = { PatchGraph::modulationOutput() };
        PatchGraph::Node targetA; targetA.id = "A"; targetA.type = PatchGraph::NodeType::processor; targetA.ports = { PatchGraph::modulationInput() };
        PatchGraph::Node targetB = targetA; targetB.id = "B";
        if (! editable.addNode (mod) || ! editable.addNode (targetA) || ! editable.addNode (targetB)) return fail ("editable graph fixture setup failed");
        PatchGraph::Edge route; route.id = "route"; route.fromNode = "MOD"; route.fromPort = "mod.out"; route.toNode = "A"; route.toPort = "mod.in";
        route.type = PatchGraph::PortType::modulation; route.editable = true;
        if (! editable.addEdge (route)) return fail ("editable route fixture rejected valid route");
        auto reconnected = route; reconnected.toNode = "B";
        if (! editable.replaceEdge ("route", reconnected, &mutationReason)) return fail ("editable route endpoint could not be reconnected");
        if (editable.findEdge ("route") == nullptr || editable.findEdge ("route")->toNode != "B") return fail ("reconnected route did not replace the endpoint");
        if (! editable.removeEdge ("route", &mutationReason) || editable.findEdge ("route") != nullptr) return fail ("editable route could not be removed");

        PatchGraph::Document cycle;
''')

# Static contracts.
replace_once(
    static,
'''    'typed persistent patch graph': ['schemaVersion', 'PortType', 'validateConnection', 'wouldCreateAudioCycle', 'topologicalOrder', 'toValueTree', 'fromValueTree', 'modulationSafe'],
''',
'''    'typed persistent patch graph': ['schemaVersion', 'PortType', 'validateConnection', 'wouldCreateAudioCycle', 'topologicalOrder', 'toValueTree', 'fromValueTree', 'modulationSafe'],
    'cable editor v1': ['findEdge', 'replaceEdge', 'removeEdge', 'showReconnectRouteMenu', 'deleteSelectedCable', 'undoCableEdit', 'redoCableEdit', 'KeyPress::deleteKey'],
''')
replace_once(
    static,
'''    'typed persistent patch graph': patch_graph + signal_page + processor_h,
''',
'''    'typed persistent patch graph': patch_graph + signal_page + processor_h,
    'cable editor v1': patch_graph + signal_page,
''')

# Plan progress: this block deliberately does not claim arbitrary audio routing.
text = plan.read_text(encoding="utf-8")
replacements = {
    '- [ ] Graph edits must be undoable and serializable before arbitrary routing is enabled.': '- [x] Currently supported graph edits are undoable and serializable before arbitrary audio routing is enabled.',
    '- [ ] Drag from output port to compatible input port to create a connection.': '- [x] Drag from the MOD output jack to a compatible modulation input to create a DSP-backed connection.',
    '- [ ] Drag an existing cable endpoint to reconnect it.': '- [x] Drag an existing modulation cable destination endpoint to reconnect it while preserving source/depth.',
    '- [ ] Click cable to select; Delete/right-click removes it.': '- [x] Click cable to select; Delete removes editable routes/layer combines; right-click or double-click opens detailed editing.',
    '- [ ] Distinct visual language for audio, modulation and clock cables.': '- [x] Distinct visual language for audio, modulation and clock cables.',
    '- [ ] Undo/redo all graph operations.': '- [x] Undo/redo for supported graph operations: node layout, modulation cable create/edit/reconnect/delete and layer-combine edits.',
    '- [ ] Do not serialize temporary meters/hover/selection state.': '- [x] Temporary meters/hover/selection and undo history are not serialized.',
    '3. **Cable editor** — connection creation/reconnection/deletion + undo.': '3. **Cable editor** — connection creation/reconnection/deletion + undo. *(v1 implemented for DSP-backed modulation and layer-combine cables; fixed audio topology remains protected)*',
}
for old, new in replacements.items():
    if old not in text:
        raise RuntimeError(f"plan.md missing anchor: {old}")
    text = text.replace(old, new, 1)
plan.write_text(text, encoding="utf-8")

print("Cable Editor v1 migration applied")
