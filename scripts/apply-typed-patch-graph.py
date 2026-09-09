#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{path}: expected exactly one anchor, found {count}: {old[:90]!r}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

# Allow semantically distinct modulation routes to share the same stage jack.
patch_graph = ROOT / 'Source/Engine/PatchGraph.h'
replace_once(patch_graph,
'''            if (edge.fromNode == candidate.fromNode && edge.fromPort == candidate.fromPort
                && edge.toNode == candidate.toNode && edge.toPort == candidate.toPort)
                return ValidationResult::failure ("Connection already exists", candidate.id);
            if (! toPort->acceptsMultiple && edge.toNode == candidate.toNode && edge.toPort == candidate.toPort)
''',
'''            if (! toPort->acceptsMultiple
                && edge.fromNode == candidate.fromNode && edge.fromPort == candidate.fromPort
                && edge.toNode == candidate.toNode && edge.toPort == candidate.toPort)
                return ValidationResult::failure ("Connection already exists", candidate.id);
            if (! toPort->acceptsMultiple && edge.toNode == candidate.toNode && edge.toPort == candidate.toPort)
''')

signal = ROOT / 'Source/UI/SignalLabPage.h'
replace_once(signal,
'''    explicit SignalLabPage (RetroMatchSynthAudioProcessor& p, bool mapOnly = false) : proc (p), mapOnlyMode (mapOnly)
    {
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }
''',
'''    explicit SignalLabPage (RetroMatchSynthAudioProcessor& p, bool mapOnly = false) : proc (p), mapOnlyMode (mapOnly)
    {
        const auto restored = proc.getPatchGraphDocument();
        graphPan = restored.view.pan;
        graphZoom = restored.view.zoom;
        snapToGrid = restored.view.snapToGrid;
        lastPersistedGraphFingerprint = restored.fingerprint();
        for (const auto& node : restored.nodes)
            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = node.position;
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }
''')

replace_once(signal,
'''        if (connecting)
        {
            connectionDragPoint = e.position;
            hoverTargetId.clear();
            if (const int target = hitTestModInput (e.position); target >= 0)
            {
                const int from = findNodeIndex (connectionFromId);
                if (from >= 0 && canConnect (nodes[(size_t) from], nodes[(size_t) target]))
                    hoverTargetId = nodes[(size_t) target].id.toStdString();
            }
            repaint();
            return;
        }
''',
'''        if (connecting)
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
''')

replace_once(signal,
'''            const int from = findNodeIndex (fromId);
            const int target = findNodeIndex (targetId);
            if (from >= 0 && target >= 0 && canConnect (nodes[(size_t) from], nodes[(size_t) target]))
                showNewRouteMenu (nodes[(size_t) from].layer, nodes[(size_t) target].stage);

            connectionDragPoint = e.position;
            repaint();
            return;
''',
'''            const int from = findNodeIndex (fromId);
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
''')

replace_once(signal,
'''        panning = false;
        draggingNode = false;
        draggingNodeId.clear();
''',
'''        panning = false;
        draggingNode = false;
        draggingNodeId.clear();
        stageGraphStateForPersistence();
''')

replace_once(signal,
'''    std::vector<ToolbarButton> toolbarButtons;
    std::map<std::string, juce::Point<float>> nodeOffsets;
    float graphZoom = 0.82f;
    juce::Point<float> graphPan { 18.0f, 18.0f };
    bool panning = false, draggingNode = false, snapToGrid = true, connecting = false;
''',
'''    std::vector<ToolbarButton> toolbarButtons;
    std::map<std::string, juce::Point<float>> nodeOffsets;
    std::map<std::string, juce::Point<float>> restoredNodePositions;
    PatchGraph::Document graphModel;
    juce::String lastPersistedGraphFingerprint, pendingGraphFingerprint;
    juce::String graphValidationMessage, connectionValidationMessage;
    bool graphStateDirty = false;
    float graphZoom = 0.82f;
    juce::Point<float> graphPan { 18.0f, 18.0f };
    bool panning = false, draggingNode = false, snapToGrid = true, connecting = false;
''')

replace_once(signal,
'''    bool canConnect (const GraphNode& from, const GraphNode& to) const
    {
        return from.role == NodeRole::modHub && from.modOutputPort
            && to.role == NodeRole::stage && to.modInputPort
            && from.layer == to.layer && to.stage >= 1 && to.stage <= 3;
    }

    ToolbarAction hitToolbar (juce::Point<float> point) const
''',
'''    PatchGraph::ValidationResult validateModConnection (const GraphNode& from, const GraphNode& to) const
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
        if (graphModel.nodes.empty()) return;
        graphModel.view.pan = graphPan;
        graphModel.view.zoom = graphZoom;
        graphModel.view.snapToGrid = snapToGrid;
        for (const auto& node : nodes)
            if (auto* stored = graphModel.findNode (node.id))
            {
                stored->position = node.worldBounds.getPosition();
                stored->positionValid = true;
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
''')

replace_once(signal,
'''            case ToolbarAction::autoArrange:
                nodeOffsets.clear();
                for (auto& node : nodes) node.worldBounds = node.baseBounds;
                fitToView();
                break;
''',
'''            case ToolbarAction::autoArrange:
                nodeOffsets.clear();
                restoredNodePositions.clear();
                for (auto& node : nodes) node.worldBounds = node.baseBounds;
                fitToView();
                break;
''')
replace_once(signal,
'''            case ToolbarAction::expand: showPatchMapOverlay(); break;
            case ToolbarAction::none: break;
        }
    }
''',
'''            case ToolbarAction::expand: showPatchMapOverlay(); break;
            case ToolbarAction::none: break;
        }
        stageGraphStateForPersistence();
    }
''')

replace_once(signal,
'''        graphPan = screenPoint - graphViewport.getPosition() - before * graphZoom;
        repaint();
    }
''',
'''        graphPan = screenPoint - graphViewport.getPosition() - before * graphZoom;
        stageGraphStateForPersistence();
        repaint();
    }
''')
replace_once(signal,
'''        graphPan = graphViewport.getCentre() - graphViewport.getPosition() - world.getCentre() * graphZoom;
        repaint();
    }
''',
'''        graphPan = graphViewport.getCentre() - graphViewport.getPosition() - world.getCentre() * graphZoom;
        stageGraphStateForPersistence();
        repaint();
    }
''')

replace_once(signal,
'''    int addNode (juce::Rectangle<float> base, const juce::String& id, const juce::String& title,
                 const juce::String& detail, const juce::String& tab, int layer, juce::Colour colour,
                 bool inputPort = true, bool outputPort = true, NodeRole role = NodeRole::stage,
                 int stage = -1, bool modInputPort = false, bool modOutputPort = false)
    {
        auto offset = juce::Point<float>();
        if (const auto it = nodeOffsets.find (id.toStdString()); it != nodeOffsets.end()) offset = it->second;
        GraphNode node;
        node.id = id; node.baseBounds = base; node.worldBounds = base.translated (offset.x, offset.y);
        node.title = title; node.detail = detail; node.tab = tab; node.layer = layer; node.colour = colour;
        node.inputPort = inputPort; node.outputPort = outputPort; node.role = role; node.stage = stage;
        node.modInputPort = modInputPort; node.modOutputPort = modOutputPort;
        nodes.push_back (std::move (node));
        return (int) nodes.size() - 1;
    }

    void connect (int from, int to, EdgeKind kind = EdgeKind::audio, EdgeEditKind editKind = EdgeEditKind::none,
                  int layer = -2, int slot = -1, const juce::String& key = {}, const juce::String& label = {})
    {
        if (juce::isPositiveAndBelow (from, (int) nodes.size()) && juce::isPositiveAndBelow (to, (int) nodes.size()))
            edges.push_back ({ from, to, kind, editKind, layer, slot, key, label });
    }
''',
'''    int addNode (juce::Rectangle<float> base, const juce::String& id, const juce::String& title,
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

        PatchGraph::Node modelNode;
        modelNode.id = id;
        modelNode.title = title;
        modelNode.position = node.worldBounds.getPosition();
        modelNode.positionValid = true;
        if (role == NodeRole::master) modelNode.type = PatchGraph::NodeType::master;
        else if (role == NodeRole::clock) modelNode.type = PatchGraph::NodeType::clock;
        else if (role == NodeRole::modHub) modelNode.type = PatchGraph::NodeType::modulationRouter;
        else if (stage == 0) modelNode.type = PatchGraph::NodeType::source;
        else if (stage == 5) modelNode.type = PatchGraph::NodeType::mixer;
        else modelNode.type = PatchGraph::NodeType::processor;
        if (inputPort) modelNode.ports.push_back (PatchGraph::audioInput (id == "GLOBALBUS"));
        if (outputPort) modelNode.ports.push_back (PatchGraph::audioOutput());
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
        edges.push_back ({ from, to, kind, editKind, layer, slot, key, label });
    }
''')

replace_once(signal,
'''        auto textArea = header.withTrimmedRight (total + 12.0f).reduced (8, 0);
        g.setColour (led); g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        const bool daw = parameter ("tempoSource", 1.0f) >= 0.5f;
        g.drawFittedText ("PATCH MAP / LEFT-CLICK EDITABLE CABLE / DRAG MOD JACK TO ADD / BIG = LARGE OVERLAY / FIXED AUDIO ORDER STAYS SAFE    CLOCK: "
                          + juce::String (proc.getEffectiveBpm(), 1) + " BPM " + (daw ? "DAW" : "MANUAL"),
                          textArea.toNearestInt(), juce::Justification::centredLeft, 1);
''',
'''        auto textArea = header.withTrimmedRight (total + 12.0f).reduced (8, 0);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        const bool daw = parameter ("tempoSource", 1.0f) >= 0.5f;
        juce::String status = "PATCH MAP / TYPED + PERSISTENT GRAPH / DRAG MOD JACK TO ADD / BIG = LARGE OVERLAY    CLOCK: "
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
''')

replace_once(signal,
'''        nodes.clear(); edges.clear();
        std::vector<int> instances { -1 };
''',
'''        nodes.clear(); edges.clear();
        graphModel.clearTopology();
        graphValidationMessage.clear();
        std::vector<int> instances { -1 };
''')
replace_once(signal,
'''        drawConnectionPreview (g, led, accent);
        g.restoreState();
    }

    void timerCallback() override
    {
''',
'''        drawConnectionPreview (g, led, accent);
        g.restoreState();
        stageGraphStateForPersistence();
    }

    void timerCallback() override
    {
        flushGraphStatePersistence();
''')

# Smoke tests for graph typing, cycle rejection, ordering and persistence.
smoke = ROOT / 'Tests/SmokeTests.cpp'
replace_once(smoke,
'''#include "../Source/Engine/MSEG.h"
''',
'''#include "../Source/Engine/MSEG.h"
#include "../Source/Engine/PatchGraph.h"
''')
replace_once(smoke,
'''    if (! runMelodyTests()) return 1;
    {
        SoundFeatures guitar;
''',
'''    if (! runMelodyTests()) return 1;
    {
        PatchGraph::Document graph;
        PatchGraph::Node source; source.id = "SRC"; source.type = PatchGraph::NodeType::source; source.ports = { PatchGraph::audioOutput() };
        source.position = { 11.0f, 22.0f }; source.positionValid = true;
        PatchGraph::Node filter; filter.id = "FILTER"; filter.type = PatchGraph::NodeType::processor;
        filter.ports = { PatchGraph::audioInput(), PatchGraph::audioOutput(), PatchGraph::modulationInput() };
        PatchGraph::Node master; master.id = "MASTER"; master.type = PatchGraph::NodeType::master; master.ports = { PatchGraph::audioInput() };
        if (! graph.addNode (source) || ! graph.addNode (filter) || ! graph.addNode (master)) return fail ("typed patch graph rejected valid nodes");
        PatchGraph::Edge a; a.id = "a"; a.fromNode = "SRC"; a.fromPort = "audio.out"; a.toNode = "FILTER"; a.toPort = "audio.in"; a.type = PatchGraph::PortType::audio;
        PatchGraph::Edge b; b.id = "b"; b.fromNode = "FILTER"; b.fromPort = "audio.out"; b.toNode = "MASTER"; b.toPort = "audio.in"; b.type = PatchGraph::PortType::audio;
        if (! graph.addEdge (a) || ! graph.addEdge (b) || ! graph.validate().ok) return fail ("typed patch graph rejected valid audio chain");
        const auto order = graph.topologicalOrder();
        if (order.size() != 3 || order[0] != "SRC" || order[1] != "FILTER" || order[2] != "MASTER")
            return fail ("patch graph topological order is not deterministic");
        PatchGraph::Edge wrong = a; wrong.id = "wrong"; wrong.toPort = "mod.in";
        if (graph.validateConnection (wrong).ok) return fail ("patch graph allowed incompatible port types");

        PatchGraph::Document cycle;
        PatchGraph::Node x; x.id = "A"; x.type = PatchGraph::NodeType::processor; x.ports = { PatchGraph::audioInput (true), PatchGraph::audioOutput() };
        PatchGraph::Node y = x; y.id = "B";
        if (! cycle.addNode (x) || ! cycle.addNode (y)) return fail ("cycle fixture node setup failed");
        PatchGraph::Edge ab; ab.id = "ab"; ab.fromNode = "A"; ab.fromPort = "audio.out"; ab.toNode = "B"; ab.toPort = "audio.in"; ab.type = PatchGraph::PortType::audio;
        PatchGraph::Edge ba; ba.id = "ba"; ba.fromNode = "B"; ba.fromPort = "audio.out"; ba.toNode = "A"; ba.toPort = "audio.in"; ba.type = PatchGraph::PortType::audio;
        if (! cycle.addEdge (ab) || cycle.validateConnection (ba).ok) return fail ("patch graph allowed a zero-delay audio cycle");

        graph.view.pan = { 33.0f, -17.0f }; graph.view.zoom = 1.37f; graph.view.snapToGrid = false;
        const auto roundTrip = PatchGraph::Document::fromValueTree (graph.toValueTree());
        const auto* restored = roundTrip.findNode ("SRC");
        if (restored == nullptr || ! restored->positionValid || std::abs (restored->position.x - 11.0f) > 0.001f
            || std::abs (roundTrip.view.zoom - 1.37f) > 0.001f || roundTrip.view.snapToGrid)
            return fail ("patch graph session round-trip lost layout or viewport state");
    }
    {
        SoundFeatures guitar;
''')

# Static source contract.
static = ROOT / 'scripts/static-check.py'
replace_once(static,
'''signal_page_path = ROOT / 'Source/UI/SignalLabPage.h'
''',
'''signal_page_path = ROOT / 'Source/UI/SignalLabPage.h'
patch_graph_path = ROOT / 'Source/Engine/PatchGraph.h'
''')
replace_once(static,
'''if not signal_page_path.exists(): errors.append('Source/UI/SignalLabPage.h is missing')
''',
'''if not signal_page_path.exists(): errors.append('Source/UI/SignalLabPage.h is missing')
if not patch_graph_path.exists(): errors.append('Source/Engine/PatchGraph.h is missing')
''')
replace_once(static,
'''signal_page = signal_page_path.read_text(encoding='utf-8') if signal_page_path.exists() else ''
matcher = (ROOT / 'Source/Matching/SoundMatcher.cpp').read_text(encoding='utf-8')
''',
'''signal_page = signal_page_path.read_text(encoding='utf-8') if signal_page_path.exists() else ''
patch_graph = patch_graph_path.read_text(encoding='utf-8') if patch_graph_path.exists() else ''
matcher = (ROOT / 'Source/Matching/SoundMatcher.cpp').read_text(encoding='utf-8')
''')
replace_once(static,
'''    'interactive signal map': ['mouseWheelMove', 'mouseDrag', 'mouseDoubleClick', 'graphZoom', 'graphPan', 'INSTANCE 1 / MAIN'],
''',
'''    'interactive signal map': ['mouseWheelMove', 'mouseDrag', 'mouseDoubleClick', 'graphZoom', 'graphPan', 'INSTANCE 1 / MAIN'],
    'typed persistent patch graph': ['schemaVersion', 'PortType', 'validateConnection', 'wouldCreateAudioCycle', 'topologicalOrder', 'toValueTree', 'fromValueTree', 'modulationSafe'],
''')
replace_once(static,
'''    'interactive signal map': signal_page,
''',
'''    'interactive signal map': signal_page,
    'typed persistent patch graph': patch_graph + signal_page + processor_h,
''')

# Plan status for this completed implementation block.
plan = ROOT / 'plan.md'
text = plan.read_text(encoding='utf-8')
updates = {
    '- [ ] Old sessions without graph state must load into the current canonical signal chain.': '- [x] Old sessions without graph state load into the current canonical signal chain; Patch Map rebuilds the typed graph from restored DSP/APVTS state.',
    '- [ ] Persist custom node positions, pan, zoom and grid preference in UI/session state.': '- [x] Persist custom node positions, pan, zoom and grid preference in UI/session state.',
    '- [ ] Only compatible port types can connect.': '- [x] Only compatible port types can connect.',
    '- [ ] Audio must eventually resolve to MASTER OUT.': '- [x] Audio must eventually resolve to MASTER OUT.',
    '- [ ] No accidental zero-delay audio cycles.': '- [x] No accidental zero-delay audio cycles.',
    '- [ ] Modulation cannot target parameters that are not modulation-safe.': '- [x] Modulation cannot target parameters that are not modulation-safe.',
    '- [ ] A node cannot own invalid duplicate inputs unless its topology explicitly supports mixing.': '- [x] A node cannot own invalid duplicate inputs unless its topology explicitly supports mixing.',
    '- [ ] Show a clear reason when a cable drop is rejected.': '- [x] Show a clear reason when a cable drop is rejected.',
    '- [ ] Topological ordering is deterministic and independent of visual node position.': '- [x] Topological ordering is deterministic and independent of visual node position.',
    '- [ ] Serialize graph version, nodes, edges, positions and UI viewport into `.rmsynth` and DAW state.': '- [x] Serialize graph version, nodes, edges, positions and UI viewport into `.rmsynth` and DAW state.',
    '- [ ] Migrate legacy sessions to the canonical default graph.': '- [x] Migrate legacy sessions without graph state to the canonical graph rebuilt from their restored synth state.',
    '- [ ] Add graph schema versioning independent of plug-in version.': '- [x] Add graph schema versioning independent of plug-in version.',
    '- [ ] Validate and repair malformed graph state instead of crashing.': '- [x] Validate and repair malformed graph state instead of crashing; invalid nodes/edges are skipped during ValueTree restore.',
    '- [ ] Static contracts for graph model, validation and append-only automation.': '- [x] Static contracts for graph model, validation and append-only automation.',
    '- [ ] Unit tests for allowed/forbidden connections.': '- [x] Unit tests for allowed/forbidden connections.',
    '- [ ] Topological-sort and cycle-detection tests.': '- [x] Topological-sort and cycle-detection tests.',
    '- [ ] Session round-trip tests for custom graph + node layout.': '- [x] Session round-trip tests for typed graph + custom node layout/view state.',
    '2. **Persistent graph data model** — stable IDs, typed ports, edge serialization, validation.': '2. **Persistent graph data model** — stable IDs, typed ports, edge serialization, validation. *(implemented; typed schema v1 persisted in session/preset state)*',
}
for old, new in updates.items():
    if old not in text:
        raise RuntimeError(f'plan.md anchor missing: {old}')
    text = text.replace(old, new, 1)
plan.write_text(text, encoding='utf-8')

print('typed patch graph integration applied')
