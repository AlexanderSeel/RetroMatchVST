#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_count(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


# Engine-neutral immutable routing plan. Compilation may allocate on the UI/state
# thread; publication and snapshot are a single packed atomic on the audio thread.
write("Source/Engine/DspRoutingPlan.h", r'''#pragma once
#include "PatchGraph.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <set>
#include <vector>

namespace DspRouting
{
static constexpr int maxLayerCount = 7;

struct Plan
{
    std::array<int, maxLayerCount> layerOrder {{ 0, 1, 2, 3, 4, 5, 6 }};
    bool graphAuthored = false;

    bool validPermutation() const noexcept
    {
        std::array<bool, maxLayerCount> seen {};
        for (const int layer : layerOrder)
        {
            if (layer < 0 || layer >= maxLayerCount || seen[(size_t) layer]) return false;
            seen[(size_t) layer] = true;
        }
        return true;
    }
};

struct CompileResult
{
    Plan plan;
    PatchGraph::ValidationResult validation;
};

inline CompileResult compile (const PatchGraph::Document& graph)
{
    CompileResult result;
    if (graph.nodes.empty() && graph.edges.empty()) return result;

    result.validation = graph.validate();
    if (! result.validation.ok) return result;

    struct OrderedLayer { int layer = 0; int sequence = 0; };
    std::vector<OrderedLayer> ordered;
    ordered.reserve (maxLayerCount);
    std::set<int> seen;

    for (const auto& edge : graph.edges)
    {
        if (edge.type != PatchGraph::PortType::audio || edge.toNode != "GLOBALBUS" || ! edge.id.startsWith ("combine:"))
            continue;

        const auto suffix = edge.id.substring (8);
        if (suffix.isEmpty() || ! suffix.containsOnly ("0123456789"))
        {
            result.validation = PatchGraph::ValidationResult::failure ("Invalid layer-combine edge ID: " + edge.id, edge.id);
            return result;
        }

        const int layer = suffix.getIntValue();
        if (layer < 0 || layer >= maxLayerCount || ! seen.insert (layer).second)
        {
            result.validation = PatchGraph::ValidationResult::failure ("Invalid or duplicate layer-combine route: " + edge.id, edge.id);
            return result;
        }
        ordered.push_back ({ layer, edge.sequence >= 0 ? edge.sequence : layer });
    }

    std::sort (ordered.begin(), ordered.end(), [] (const OrderedLayer& a, const OrderedLayer& b)
    {
        if (a.sequence != b.sequence) return a.sequence < b.sequence;
        return a.layer < b.layer;
    });

    std::array<bool, maxLayerCount> emitted {};
    size_t out = 0;
    for (const auto& item : ordered)
    {
        result.plan.layerOrder[out++] = item.layer;
        emitted[(size_t) item.layer] = true;
    }
    for (int layer = 0; layer < maxLayerCount; ++layer)
        if (! emitted[(size_t) layer]) result.plan.layerOrder[out++] = layer;

    result.plan.graphAuthored = ! ordered.empty();
    if (! result.plan.validPermutation())
        result.validation = PatchGraph::ValidationResult::failure ("Compiled layer routing is not a valid permutation");
    return result;
}

class AtomicPlan
{
public:
    AtomicPlan() noexcept { publish (Plan {}); }

    void publish (const Plan& plan) noexcept
    {
        const auto safe = plan.validPermutation() ? plan : Plan {};
        packed.store (pack (safe), std::memory_order_release);
    }

    Plan snapshot() const noexcept
    {
        return unpack (packed.load (std::memory_order_acquire));
    }

private:
    std::atomic<std::uint32_t> packed { 0 };

    static std::uint32_t pack (const Plan& plan) noexcept
    {
        std::uint32_t value = plan.graphAuthored ? 0x80000000u : 0u;
        for (int i = 0; i < maxLayerCount; ++i)
            value |= (std::uint32_t) (plan.layerOrder[(size_t) i] & 0x7) << (i * 3);
        return value;
    }

    static Plan unpack (std::uint32_t value) noexcept
    {
        Plan plan;
        plan.graphAuthored = (value & 0x80000000u) != 0;
        for (int i = 0; i < maxLayerCount; ++i)
            plan.layerOrder[(size_t) i] = (int) ((value >> (i * 3)) & 0x7u);
        return plan.validPermutation() ? plan : Plan {};
    }
};
}
''')

# Patch graph schema v2: layer-combine edges carry deterministic processing order.
path = "Source/Engine/PatchGraph.h"
text = read(path)
text = replace_once(text, '#include <string>\n#include <vector>\n', '#include <string>\n#include <utility>\n#include <vector>\n', "PatchGraph utility include")
text = replace_once(text, 'static constexpr int schemaVersion = 1;', 'static constexpr int schemaVersion = 2;', "PatchGraph schema")
text = replace_once(text,
'''    PortType type = PortType::audio;\n    bool editable = false;\n};''',
'''    PortType type = PortType::audio;\n    bool editable = false;\n    int sequence = -1;\n};''', "PatchGraph edge sequence")
text = replace_once(text,
'''            child.setProperty ("type", (int) edge.type, nullptr);\n            child.setProperty ("editable", edge.editable, nullptr);\n            edgeTree.appendChild (child, nullptr);''',
'''            child.setProperty ("type", (int) edge.type, nullptr);\n            child.setProperty ("editable", edge.editable, nullptr);\n            child.setProperty ("sequence", edge.sequence, nullptr);\n            edgeTree.appendChild (child, nullptr);''', "PatchGraph serialize sequence")
text = replace_once(text,
'''            edge.type = (PortType) juce::jlimit (0, (int) PortType::control, (int) child.getProperty ("type", 0));\n            edge.editable = (bool) child.getProperty ("editable", false);\n            juce::String reason;''',
'''            edge.type = (PortType) juce::jlimit (0, (int) PortType::control, (int) child.getProperty ("type", 0));\n            edge.editable = (bool) child.getProperty ("editable", false);\n            edge.sequence = (int) child.getProperty ("sequence", -1);\n            juce::String reason;''', "PatchGraph restore sequence")
text = replace_once(text,
'''                << edge.toNode << ":" << edge.toPort << ":" << (int) edge.type << ":" << (edge.editable ? 1 : 0);''',
'''                << edge.toNode << ":" << edge.toPort << ":" << (int) edge.type << ":" << (edge.editable ? 1 : 0)\n                << ":" << edge.sequence;''', "PatchGraph fingerprint sequence")
write(path, text)

# SynthEngine consumes the precompiled order and never reads graph state itself.
path = "Source/Engine/SynthEngine.h"
text = read(path)
text = replace_once(text, '#include "ModuleRack.h"\n#include "TempoSync.h"', '#include "ModuleRack.h"\n#include "DspRoutingPlan.h"\n#include "TempoSync.h"', "SynthEngine routing include")
text = replace_once(text,
'''    float mainLayerGain = 1.0f;\n};\n\nclass HybridVoice''',
'''    float mainLayerGain = 1.0f;\n};\n\nstatic_assert (VoiceParameters::extraLayerCount == DspRouting::maxLayerCount);\n\nclass HybridVoice''', "SynthEngine layer count contract")
text = replace_once(text,
'''    void render (juce::AudioBuffer<float>&, juce::MidiBuffer&);\n    void reset();''',
'''    void render (juce::AudioBuffer<float>&, juce::MidiBuffer&);\n    void setRoutingPlan (const DspRouting::Plan& plan) noexcept\n    {\n        routingPlan = plan.validPermutation() ? plan : DspRouting::Plan {};\n    }\n    void reset();''', "SynthEngine routing setter")
text = replace_once(text,
'''    juce::AudioBuffer<float> layerScratch;\n    VoiceParameters current;\n    ModuleRack moduleRack;''',
'''    juce::AudioBuffer<float> layerScratch;\n    VoiceParameters current;\n    DspRouting::Plan routingPlan;\n    ModuleRack moduleRack;''', "SynthEngine routing member")
write(path, text)

path = "Source/Engine/SynthEngine.cpp"
text = read(path)
text = replace_once(text,
'''    for (size_t i = 0; i < layerEngines.size(); ++i)\n    {\n        auto* layer = layerEngines[i].get();''',
'''    for (int orderSlot = 0; orderSlot < DspRouting::maxLayerCount; ++orderSlot)\n    {\n        const int layerIndex = routingPlan.layerOrder[(size_t) orderSlot];\n        if (! juce::isPositiveAndBelow (layerIndex, (int) layerEngines.size())) continue;\n        const size_t i = (size_t) layerIndex;\n        auto* layer = layerEngines[i].get();''', "SynthEngine ordered layer render")
write(path, text)

# Processor compiles/publishes on the state/UI side and snapshots once per audio block.
path = "Source/PluginProcessor.h"
text = read(path)
text = replace_once(text,
'''    void setPatchGraphDocument (const PatchGraph::Document& graph)\n    {\n        auto previous = apvts.state.getChildWithName ("PATCH_GRAPH");\n        if (previous.isValid()) apvts.state.removeChild (previous, nullptr);\n        apvts.state.appendChild (graph.toValueTree(), nullptr);\n    }''',
'''    void setPatchGraphDocument (const PatchGraph::Document& graph)\n    {\n        const auto compiled = DspRouting::compile (graph);\n        if (! compiled.validation.ok) return;\n        auto previous = apvts.state.getChildWithName ("PATCH_GRAPH");\n        if (previous.isValid()) apvts.state.removeChild (previous, nullptr);\n        apvts.state.appendChild (graph.toValueTree(), nullptr);\n        routingPlanPublisher.publish (compiled.plan);\n    }''', "Processor graph compile/publish")
text = replace_once(text,
'''    SynthEngine engine;\n    // True whole-instrument bus: runs once after main + all layer instances are combined.''',
'''    SynthEngine engine;\n    DspRouting::AtomicPlan routingPlanPublisher;\n    // True whole-instrument bus: runs once after main + all layer instances are combined.''', "Processor atomic routing member")
text = replace_once(text,
'''    void invalidateMatchesAfterReferencePitchChange();\n    void delayReferenceForLatency (juce::AudioBuffer<float>&);''',
'''    void invalidateMatchesAfterReferencePitchChange();\n    void rebuildRoutingPlanFromState();\n    void delayReferenceForLatency (juce::AudioBuffer<float>&);''', "Processor routing rebuild declaration")
write(path, text)

path = "Source/PluginProcessor.cpp"
text = read(path)
text = replace_once(text,
'''bool RetroMatchSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const\n{\n    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();\n}\n\nVoiceParameters RetroMatchSynthAudioProcessor::readParams''',
'''bool RetroMatchSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const\n{\n    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();\n}\n\nvoid RetroMatchSynthAudioProcessor::rebuildRoutingPlanFromState()\n{\n    const auto graph = getPatchGraphDocument();\n    const auto compiled = DspRouting::compile (graph);\n    routingPlanPublisher.publish (compiled.validation.ok ? compiled.plan : DspRouting::Plan {});\n}\n\nVoiceParameters RetroMatchSynthAudioProcessor::readParams''', "Processor routing rebuild implementation")
text = replace_once(text,
'''    const auto mode = getReferenceAuditionMode();\n    engine.setParameters (readParams());\n    engine.render (b, renderMidi);''',
'''    const auto mode = getReferenceAuditionMode();\n    engine.setRoutingPlan (routingPlanPublisher.snapshot());\n    engine.setParameters (readParams());\n    engine.render (b, renderMidi);''', "Processor block-boundary routing snapshot")
text = replace_count(text,
'''            restoreLayers();\n            analysisStartSeconds.store''',
'''            restoreLayers();\n            rebuildRoutingPlanFromState();\n            analysisStartSeconds.store''', 1, "Processor DAW state routing restore")
text = replace_count(text,
'''    restoreLayers();\n    analysisStartSeconds.store''',
'''    restoreLayers();\n    rebuildRoutingPlanFromState();\n    analysisStartSeconds.store''', 1, "Processor preset routing restore")
write(path, text)

# Offline renderer accepts an explicit compiled plan for DSP regression tests and future topology search.
path = "Source/Matching/OfflineRenderer.h"
text = read(path)
text = replace_once(text,
'''                                                  float targetFundamentalHz,\n                                                  int blockSize = 256);''',
'''                                                  float targetFundamentalHz,\n                                                  int blockSize = 256,\n                                                  DspRouting::Plan routingPlan = {});''', "OfflineRenderer plan signature")
write(path, text)

path = "Source/Matching/OfflineRenderer.cpp"
text = read(path)
text = replace_once(text,
'''                                                        float targetFundamentalHz,\n                                                        int blockSize)''',
'''                                                        float targetFundamentalHz,\n                                                        int blockSize,\n                                                        DspRouting::Plan routingPlan)''', "OfflineRenderer plan implementation signature")
text = replace_once(text,
'''    engine.setRandomSeed ((int64) 0x524d534f);\n    engine.setParameters (params);''',
'''    engine.setRandomSeed ((int64) 0x524d534f);\n    engine.setRoutingPlan (routingPlan);\n    engine.setParameters (params);''', "OfflineRenderer apply plan")
write(path, text)

# Signal Lab persists sequence metadata, exposes order actions and flushes immediately
# so a valid reorder becomes audible without waiting for an unrelated state change.
path = "Source/UI/SignalLabPage.h"
text = read(path)
text = replace_once(text,
'''        for (const auto& node : restored.nodes)\n            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };\n        setWantsKeyboardFocus (true);''',
'''        for (const auto& node : restored.nodes)\n            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };\n        for (const auto& edge : restored.edges)\n            if (edge.sequence >= 0) restoredEdgeSequences[edge.id.toStdString()] = edge.sequence;\n        setWantsKeyboardFocus (true);''', "SignalLab restore edge sequence")
text = replace_once(text,
'''        int layer = -2, slot = -1;\n        juce::String key, label;\n    };''',
'''        int layer = -2, slot = -1;\n        juce::String key, label;\n        int sequence = -1;\n    };''', "SignalLab GraphEdge sequence")
text = replace_once(text,
'''    std::map<std::string, juce::Point<float>> nodeOffsets;\n    std::map<std::string, juce::Point<float>> restoredNodePositions;\n    PatchGraph::Document graphModel;''',
'''    std::map<std::string, juce::Point<float>> nodeOffsets;\n    std::map<std::string, juce::Point<float>> restoredNodePositions;\n    std::map<std::string, int> restoredEdgeSequences;\n    PatchGraph::Document graphModel;''', "SignalLab sequence map")
text = replace_once(text,
'''        restoredNodePositions.clear(); nodeOffsets.clear();\n        for (const auto& node : state.graph.nodes)\n            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };\n        lastPersistedGraphFingerprint = state.graph.fingerprint();''',
'''        restoredNodePositions.clear(); nodeOffsets.clear(); restoredEdgeSequences.clear();\n        for (const auto& node : state.graph.nodes)\n            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };\n        for (const auto& edge : state.graph.edges)\n            if (edge.sequence >= 0) restoredEdgeSequences[edge.id.toStdString()] = edge.sequence;\n        lastPersistedGraphFingerprint = state.graph.fingerprint();''', "SignalLab undo restore sequence")
text = replace_once(text,
'''        modelEdge.toNode = nodes[(size_t) to].id;\n        modelEdge.editable = editKind != EdgeEditKind::none;\n        if (kind == EdgeKind::audio)''',
'''        modelEdge.toNode = nodes[(size_t) to].id;\n        modelEdge.editable = editKind != EdgeEditKind::none;\n        int edgeSequence = -1;\n        if (editKind == EdgeEditKind::layerCombine && key.isNotEmpty())\n        {\n            const auto stableKey = key.toStdString();\n            const auto restored = restoredEdgeSequences.find (stableKey);\n            edgeSequence = restored != restoredEdgeSequences.end() ? restored->second : juce::jmax (0, layer);\n            restoredEdgeSequences[stableKey] = edgeSequence;\n            modelEdge.sequence = edgeSequence;\n        }\n        if (kind == EdgeKind::audio)''', "SignalLab connect sequence")
text = replace_once(text,
'''        edges.push_back ({ from, to, kind, editKind, layer, slot, key, label });''',
'''        edges.push_back ({ from, to, kind, editKind, layer, slot, key, label, edgeSequence });''', "SignalLab UI edge sequence")
helpers = r'''
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
        for (int sequence = 0; sequence < (int) order.size(); ++sequence)
        {
            const auto key = "combine:" + juce::String (order[(size_t) sequence]);
            restoredEdgeSequences[key.toStdString()] = sequence;
            if (auto* model = graphModel.findEdge (key)) model->sequence = sequence;
            for (auto& edge : edges)
                if (edge.key == key) edge.sequence = sequence;
        }
        stageGraphStateForPersistence();
        flushGraphStatePersistence();
        selectedEdgeKey = ("combine:" + juce::String (layer)).toStdString();
        repaint();
        return true;
    }

'''
text = replace_once(text, '\n    void showCombineMenu (int layer)\n', '\n' + helpers + '    void showCombineMenu (int layer)\n', "SignalLab combine order helpers")
text = replace_once(text,
'''        menu.addSubMenu ("AMOUNT", amountMenu);\n        menu.addSeparator();\n        menu.addItem (9000, "DISABLE LAYER / REMOVE FROM MIX");''',
'''        menu.addSubMenu ("AMOUNT", amountMenu);\n\n        const auto currentOrder = currentCombineLayerOrder();\n        const auto orderIt = std::find (currentOrder.begin(), currentOrder.end(), layer);\n        const int orderIndex = orderIt == currentOrder.end() ? -1 : (int) std::distance (currentOrder.begin(), orderIt);\n        menu.addSeparator();\n        menu.addItem (8001, "MOVE EARLIER", orderIndex > 0);\n        menu.addItem (8002, "MOVE LATER", orderIndex >= 0 && orderIndex + 1 < (int) currentOrder.size());\n        menu.addSeparator();\n        menu.addItem (9000, "DISABLE LAYER / REMOVE FROM MIX");''', "SignalLab combine order menu")
text = replace_once(text,
'''            if (safeThis == nullptr || result == 0) return;\n            safeThis->pushUndoState();\n            if (result >= 100 && result < 105)''',
'''            if (safeThis == nullptr || result == 0) return;\n            if (result == 8001) { safeThis->moveCombineLayer (layer, -1); return; }\n            if (result == 8002) { safeThis->moveCombineLayer (layer, 1); return; }\n            safeThis->pushUndoState();\n            if (result >= 100 && result < 105)''', "SignalLab combine menu callback")
text = replace_once(text,
'''                connect (combineNodes[i], globalBus, EdgeKind::audio, EdgeEditKind::layerCombine, layer, -1,\n                         "combine:" + juce::String (layer), "EDIT / " + opNames[operation] + " " + juce::String (parameter (prefix + "Amount", 1.0f), 2));''',
'''                const auto combineKey = "combine:" + juce::String (layer);\n                const auto restoredSequence = restoredEdgeSequences.find (combineKey.toStdString());\n                const int displaySequence = restoredSequence != restoredEdgeSequences.end() ? restoredSequence->second : layer;\n                connect (combineNodes[i], globalBus, EdgeKind::audio, EdgeEditKind::layerCombine, layer, -1,\n                         combineKey, "ORDER " + juce::String (displaySequence + 1) + " / EDIT / " + opNames[operation] + " "\n                                     + juce::String (parameter (prefix + "Amount", 1.0f), 2));''', "SignalLab order label")
write(path, text)

# DSP/compiler regression coverage.
path = "Tests/SmokeTests.cpp"
text = read(path)
text = replace_once(text, '#include "../Source/Engine/PatchGraph.h"\n', '#include "../Source/Engine/PatchGraph.h"\n#include "../Source/Engine/DspRoutingPlan.h"\n', "SmokeTests routing include")
compiler_test = r'''    {
        PatchGraph::Document graph;
        auto addAudioNode = [&] (juce::String id, PatchGraph::NodeType type, bool input, bool output, bool multipleInput = false)
        {
            PatchGraph::Node node; node.id = std::move (id); node.type = type;
            if (input) node.ports.push_back (PatchGraph::audioInput (multipleInput));
            if (output) node.ports.push_back (PatchGraph::audioOutput());
            if (! graph.addNode (std::move (node))) return false;
            return true;
        };
        if (! addAudioNode ("L0:S5", PatchGraph::NodeType::mixer, false, true)
            || ! addAudioNode ("L1:S5", PatchGraph::NodeType::mixer, false, true)
            || ! addAudioNode ("GLOBALBUS", PatchGraph::NodeType::processor, true, true, true)
            || ! addAudioNode ("MASTER", PatchGraph::NodeType::master, true, false))
            return fail ("routing compiler graph fixture could not be built");

        PatchGraph::Edge layer0 { "combine:0", "L0:S5", "audio.out", "GLOBALBUS", "audio.in", PatchGraph::PortType::audio, true };
        PatchGraph::Edge layer1 { "combine:1", "L1:S5", "audio.out", "GLOBALBUS", "audio.in", PatchGraph::PortType::audio, true };
        layer0.sequence = 1; layer1.sequence = 0;
        if (! graph.addEdge (layer0) || ! graph.addEdge (layer1)
            || ! graph.addEdge ({ "global:master", "GLOBALBUS", "audio.out", "MASTER", "audio.in", PatchGraph::PortType::audio, false }))
            return fail ("routing compiler graph edges were rejected");

        const auto compiled = DspRouting::compile (graph);
        if (! compiled.validation.ok || compiled.plan.layerOrder[0] != 1 || compiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler ignored persisted layer-combine order");

        const auto restored = PatchGraph::Document::fromValueTree (graph.toValueTree());
        const auto restoredCompiled = DspRouting::compile (restored);
        if (! restoredCompiled.validation.ok || restoredCompiled.plan.layerOrder[0] != 1 || restoredCompiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler order did not survive graph state round-trip");

        DspRouting::AtomicPlan published;
        published.publish (restoredCompiled.plan);
        const auto snapshot = published.snapshot();
        if (snapshot.layerOrder != restoredCompiled.plan.layerOrder || ! snapshot.graphAuthored)
            return fail ("atomic routing plan publication changed the compiled permutation");
    }
'''
text = replace_once(text,
'''    if (! runMelodyTests()) return 1;\n    {\n        VoiceParameters fullRack;''',
'''    if (! runMelodyTests()) return 1;\n''' + compiler_test + '''    {\n        VoiceParameters fullRack;''', "SmokeTests compiler block")
order_test = r'''
        auto orderProbe = makeFactoryPreset (0);
        orderProbe.layers.fill (nullptr);
        auto orderLayerA = std::make_shared<VoiceParameters> (makeFactoryPreset (2));
        auto orderLayerB = std::make_shared<VoiceParameters> (makeFactoryPreset (4));
        orderLayerA->layers.fill (nullptr); orderLayerB->layers.fill (nullptr);
        orderProbe.layers[0] = orderLayerA; orderProbe.layers[1] = orderLayerB;
        orderProbe.mainLayerGain = 0.72f;
        orderProbe.layerGain[0] = 0.56f; orderProbe.layerGain[1] = 0.48f;
        orderProbe.layerOperation[0] = 2; orderProbe.layerAmount[0] = 0.88f;
        orderProbe.layerOperation[1] = 3; orderProbe.layerAmount[1] = 0.82f;
        DspRouting::Plan canonicalOrder;
        auto swappedOrder = canonicalOrder;
        std::swap (swappedOrder.layerOrder[0], swappedOrder.layerOrder[1]);
        swappedOrder.graphAuthored = true;
        const auto canonicalAudio = OfflineRenderer::renderPatch (orderProbe, 22050, 0.5f, 220, 128, canonicalOrder);
        const auto reorderedAudio = OfflineRenderer::renderPatch (orderProbe, 22050, 0.5f, 220, 128, swappedOrder);
        if (! finiteAudio (canonicalAudio) || ! finiteAudio (reorderedAudio)
            || canonicalAudio.getMagnitude (0, canonicalAudio.getNumSamples()) < 1.0e-5f
            || reorderedAudio.getMagnitude (0, reorderedAudio.getNumSamples()) < 1.0e-5f)
            return fail ("compiled layer routing produced silent or non-finite audio");
        if (maxDifference (canonicalAudio, reorderedAudio) < 1.0e-4f)
            return fail ("compiled layer-combine order did not create a real sonic change");
'''
text = replace_once(text,
'''        combined.layerOperation[0] = 4; combined.layerGain[0] = 0;\n        const auto dividedBySilence = OfflineRenderer::renderPatch (combined, 22050, 0.5f, 220);\n        if (! finiteAudio (dividedBySilence) || dividedBySilence.getMagnitude (0, dividedBySilence.getNumSamples()) > 1)\n            return fail ("division by silence must remain bounded");''',
'''        combined.layerOperation[0] = 4; combined.layerGain[0] = 0;\n        const auto dividedBySilence = OfflineRenderer::renderPatch (combined, 22050, 0.5f, 220);\n        if (! finiteAudio (dividedBySilence) || dividedBySilence.getMagnitude (0, dividedBySilence.getNumSamples()) > 1)\n            return fail ("division by silence must remain bounded");\n''' + order_test, "SmokeTests audible routing order")
write(path, text)

# Static source contracts include the new compiler and RT publication path.
path = "scripts/static-check.py"
text = read(path)
text = replace_once(text,
'''patch_graph_path = ROOT / 'Source/Engine/PatchGraph.h'\nif not mseg_path.exists():''',
'''patch_graph_path = ROOT / 'Source/Engine/PatchGraph.h'\ndsp_routing_path = ROOT / 'Source/Engine/DspRoutingPlan.h'\nif not mseg_path.exists():''', "static check routing path")
text = replace_once(text,
'''if not patch_graph_path.exists(): errors.append('Source/Engine/PatchGraph.h is missing')\n\nprocessor =''',
'''if not patch_graph_path.exists(): errors.append('Source/Engine/PatchGraph.h is missing')\nif not dsp_routing_path.exists(): errors.append('Source/Engine/DspRoutingPlan.h is missing')\n\nprocessor =''', "static check routing existence")
text = replace_once(text,
'''patch_graph = patch_graph_path.read_text(encoding='utf-8') if patch_graph_path.exists() else ''\nmatcher =''',
'''patch_graph = patch_graph_path.read_text(encoding='utf-8') if patch_graph_path.exists() else ''\ndsp_routing = dsp_routing_path.read_text(encoding='utf-8') if dsp_routing_path.exists() else ''\nmatcher =''', "static check routing source")
text = replace_once(text,
'''    'cable editor v1': ['findEdge', 'replaceEdge', 'removeEdge', 'showReconnectRouteMenu', 'deleteSelectedCable', 'undoCableEdit', 'redoCableEdit', 'KeyPress::deleteKey'],\n}''',
'''    'cable editor v1': ['findEdge', 'replaceEdge', 'removeEdge', 'showReconnectRouteMenu', 'deleteSelectedCable', 'undoCableEdit', 'redoCableEdit', 'KeyPress::deleteKey'],\n    'DSP routing compiler v1': ['maxLayerCount', 'layerOrder', 'CompileResult', 'AtomicPlan', 'memory_order_release', 'memory_order_acquire'],\n}''', "static check routing tokens")
text = replace_once(text,
'''    'cable editor v1': patch_graph + signal_page,\n}''',
'''    'cable editor v1': patch_graph + signal_page,\n    'DSP routing compiler v1': dsp_routing + engine_h + engine_cpp + processor + processor_h + signal_page,\n}''', "static check routing text")
write(path, text)

# Self-remove staging machinery from the product commit.
(ROOT / "scripts/apply-dsp-routing-v1.py").unlink()
(ROOT / ".github/workflows/apply-dsp-routing-v1.yml").unlink()
