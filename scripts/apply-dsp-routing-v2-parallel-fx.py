#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def read(p): return (ROOT / p).read_text(encoding="utf-8")
def write(p, t): (ROOT / p).write_text(t, encoding="utf-8")
def replace_once(t, old, new, label):
    c = t.count(old)
    if c != 1: raise RuntimeError(f"{label}: expected 1 match, found {c}")
    return t.replace(old, new, 1)

# -----------------------------------------------------------------------------
# PatchGraph schema v3: optional persisted processing mode on processor nodes.
# 0 = canonical serial; 1 = supported compensated parallel FX core.
# -----------------------------------------------------------------------------
path = "Source/Engine/PatchGraph.h"
text = read(path)
text = replace_once(text, 'static constexpr int schemaVersion = 2;', 'static constexpr int schemaVersion = 3;', "PatchGraph schema v3")
text = replace_once(text,
'''    Position position;
    bool positionValid = false;
    bool locked = false;''',
'''    Position position;
    bool positionValid = false;
    bool locked = false;
    int routingMode = 0;''', "PatchGraph node routing mode")
text = replace_once(text,
'''            child.setProperty ("locked", node.locked, nullptr);
            for (const auto& port : node.ports)''',
'''            child.setProperty ("locked", node.locked, nullptr);
            child.setProperty ("routingMode", node.routingMode, nullptr);
            for (const auto& port : node.ports)''', "PatchGraph serialize routing mode")
text = replace_once(text,
'''            node.locked = (bool) child.getProperty ("locked", false);
            for (const auto& portChild : child)''',
'''            node.locked = (bool) child.getProperty ("locked", false);
            node.routingMode = juce::jlimit (0, 1, (int) child.getProperty ("routingMode", 0));
            for (const auto& portChild : child)''', "PatchGraph restore routing mode")
text = replace_once(text,
'''                << ":" << juce::String (node.position.x, 3) << ":" << juce::String (node.position.y, 3) << ":" << (node.locked ? 1 : 0);''',
'''                << ":" << juce::String (node.position.x, 3) << ":" << juce::String (node.position.y, 3) << ":" << (node.locked ? 1 : 0)
                << ":" << node.routingMode;''', "PatchGraph fingerprint routing mode")
write(path, text)

# -----------------------------------------------------------------------------
# Immutable routing plan: one parallel-FX bit per main/layer instance, still fits
# into the original uint32 atomic publication (21 order bits + 8 topology bits).
# -----------------------------------------------------------------------------
path = "Source/Engine/DspRoutingPlan.h"
text = read(path)
text = replace_once(text,
'''static constexpr int maxLayerCount = 7;

struct Plan
{
    std::array<int, maxLayerCount> layerOrder {{ 0, 1, 2, 3, 4, 5, 6 }};
    bool graphAuthored = false;''',
'''static constexpr int maxLayerCount = 7;
static constexpr int maxInstanceCount = maxLayerCount + 1;

struct Plan
{
    std::array<int, maxLayerCount> layerOrder {{ 0, 1, 2, 3, 4, 5, 6 }};
    std::array<bool, maxInstanceCount> parallelFx {};
    bool graphAuthored = false;''', "Routing plan parallel flags")
text = replace_once(text,
'''    result.plan.graphAuthored = ! ordered.empty();
    if (! result.plan.validPermutation())''',
'''    for (int instance = 0; instance < maxInstanceCount; ++instance)
    {
        const int layer = instance - 1;
        const auto nodeId = "L" + juce::String (layer) + ":S4";
        if (const auto* fxNode = graph.findNode (nodeId); fxNode != nullptr)
            result.plan.parallelFx[(size_t) instance] = fxNode->routingMode == 1;
    }

    result.plan.graphAuthored = ! ordered.empty()
                             || std::any_of (result.plan.parallelFx.begin(), result.plan.parallelFx.end(), [] (bool value) { return value; });
    if (! result.plan.validPermutation())''', "Compile FX topology")
text = replace_once(text,
'''        std::uint32_t value = plan.graphAuthored ? 0x80000000u : 0u;
        for (int i = 0; i < maxLayerCount; ++i)
            value |= (std::uint32_t) (plan.layerOrder[(size_t) i] & 0x7) << (i * 3);
        return value;''',
'''        std::uint32_t value = plan.graphAuthored ? 0x80000000u : 0u;
        for (int i = 0; i < maxLayerCount; ++i)
            value |= (std::uint32_t) (plan.layerOrder[(size_t) i] & 0x7) << (i * 3);
        for (int i = 0; i < maxInstanceCount; ++i)
            if (plan.parallelFx[(size_t) i]) value |= 1u << (21 + i);
        return value;''', "Pack FX topology")
text = replace_once(text,
'''        plan.graphAuthored = (value & 0x80000000u) != 0;
        for (int i = 0; i < maxLayerCount; ++i)
            plan.layerOrder[(size_t) i] = (int) ((value >> (i * 3)) & 0x7u);
        return plan.validPermutation() ? plan : Plan {};''',
'''        plan.graphAuthored = (value & 0x80000000u) != 0;
        for (int i = 0; i < maxLayerCount; ++i)
            plan.layerOrder[(size_t) i] = (int) ((value >> (i * 3)) & 0x7u);
        for (int i = 0; i < maxInstanceCount; ++i)
            plan.parallelFx[(size_t) i] = (value & (1u << (21 + i))) != 0;
        return plan.validPermutation() ? plan : Plan {};''', "Unpack FX topology")
write(path, text)

# -----------------------------------------------------------------------------
# SynthEngine: preallocated parallel scratch; split PRE rack and legacy core from
# identical dry input, merge at 0.5/0.5, then POST rack/output gain exactly once.
# -----------------------------------------------------------------------------
path = "Source/Engine/SynthEngine.h"
text = read(path)
text = replace_once(text,
'''    juce::AudioBuffer<float> layerScratch;
    VoiceParameters current;''',
'''    juce::AudioBuffer<float> layerScratch;
    juce::AudioBuffer<float> parallelFxScratch;
    VoiceParameters current;''', "SynthEngine parallel scratch")
text = replace_once(text,
'''    void processEffects (juce::AudioBuffer<float>& audio);
    void compensateLatency (juce::AudioBuffer<float>& audio);''',
'''    void processEffects (juce::AudioBuffer<float>& audio);
    void processBuiltInEffects (juce::AudioBuffer<float>& audio);
    void compensateLatency (juce::AudioBuffer<float>& audio);''', "SynthEngine built-in helper declaration")
write(path, text)

path = "Source/Engine/SynthEngine.cpp"
text = read(path)
text = replace_once(text,
'''    layerScratch.setSize (safeChannels, safeBlockSize);
    moduleRack.prepare (sr, samplesPerBlock, channels);''',
'''    layerScratch.setSize (safeChannels, safeBlockSize);
    parallelFxScratch.setSize (safeChannels, safeBlockSize);
    moduleRack.prepare (sr, samplesPerBlock, channels);''', "Prepare parallel scratch")

start = text.index('void SynthEngine::processEffects (juce::AudioBuffer<float>& audio)')
end = text.index('void SynthEngine::compensateLatency (juce::AudioBuffer<float>& audio)', start)
old = text[start:end]
# Extract the existing built-in body by retaining everything between PRE rack and POST rack.
pre_line = '    moduleRack.process (audio, current.fxModules, 0, current.tempoBpm);\n'
post_line = '    moduleRack.process (audio, current.fxModules, 1, current.tempoBpm);\n    audio.applyGain (juce::Decibels::decibelsToGain (current.outputGainDb));\n'
if pre_line not in old or post_line not in old:
    raise RuntimeError("processEffects split anchors missing")
builtin = old[old.index(pre_line) + len(pre_line):old.index(post_line)]
new = '''void SynthEngine::processBuiltInEffects (juce::AudioBuffer<float>& audio)\n{\n''' + builtin + '''}\n\nvoid SynthEngine::processEffects (juce::AudioBuffer<float>& audio)\n{\n    const bool parallel = routingPlan.parallelFx[0];\n    const bool scratchReady = parallelFxScratch.getNumChannels() >= audio.getNumChannels()\n                           && parallelFxScratch.getNumSamples() >= audio.getNumSamples();\n\n    if (! parallel || ! scratchReady)\n    {\n        moduleRack.process (audio, current.fxModules, 0, current.tempoBpm);\n        processBuiltInEffects (audio);\n        moduleRack.process (audio, current.fxModules, 1, current.tempoBpm);\n        audio.applyGain (juce::Decibels::decibelsToGain (current.outputGainDb));\n        return;\n    }\n\n    // Deterministic split: both branches start with the exact same dry block.\n    // Branch A owns PRE rack processing, branch B owns the legacy built-in FX core.\n    for (int ch = 0; ch < audio.getNumChannels(); ++ch)\n        parallelFxScratch.copyFrom (ch, 0, audio, ch, 0, audio.getNumSamples());\n\n    moduleRack.process (audio, current.fxModules, 0, current.tempoBpm);\n    processBuiltInEffects (parallelFxScratch);\n\n    // Correlated-unity compensation: 0.5 + 0.5 prevents a dry/dry split from\n    // producing the +6 dB jump that a raw sum would introduce.\n    for (int ch = 0; ch < audio.getNumChannels(); ++ch)\n    {\n        auto* a = audio.getWritePointer (ch);\n        const auto* b = parallelFxScratch.getReadPointer (ch);\n        for (int i = 0; i < audio.getNumSamples(); ++i) a[i] = 0.5f * (a[i] + b[i]);\n    }\n\n    moduleRack.process (audio, current.fxModules, 1, current.tempoBpm);\n    audio.applyGain (juce::Decibels::decibelsToGain (current.outputGainDb));\n}\n\n'''
text = text[:start] + new + text[end:]
text = replace_once(text,
'''        layer->setParameters (p);
        layerActive[i] = true;''',
'''        layer->setParameters (p);
        DspRouting::Plan layerRouting;
        layerRouting.parallelFx[0] = routingPlan.parallelFx[i + 1];
        layer->setRoutingPlan (layerRouting);
        layerActive[i] = true;''', "Propagate per-layer FX topology")
write(path, text)

# -----------------------------------------------------------------------------
# Signal Lab: persist routing mode per FX node and expose a DSP-backed node menu.
# -----------------------------------------------------------------------------
path = "Source/UI/SignalLabPage.h"
text = read(path)
text = replace_once(text,
'''        for (const auto& node : restored.nodes)
            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };
        for (const auto& edge : restored.edges)''',
'''        for (const auto& node : restored.nodes)
        {
            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };
            if (node.routingMode != 0) restoredNodeRoutingModes[node.id.toStdString()] = node.routingMode;
        }
        for (const auto& edge : restored.edges)''', "Restore node routing modes")
text = replace_once(text,
'''                selectedNodeId = nodes[(size_t) node].id.toStdString();
                if (nodes[(size_t) node].role == NodeRole::modHub) showModHubMenu (nodes[(size_t) node].layer);
                repaint();''',
'''                selectedNodeId = nodes[(size_t) node].id.toStdString();
                if (nodes[(size_t) node].role == NodeRole::modHub) showModHubMenu (nodes[(size_t) node].layer);
                else if (nodes[(size_t) node].role == NodeRole::stage && nodes[(size_t) node].stage == 4) showFxTopologyMenu (nodes[(size_t) node]);
                repaint();''', "FX node right-click menu")
text = replace_once(text,
'''        NodeRole role = NodeRole::stage;
        int stage = -1;
    };''',
'''        NodeRole role = NodeRole::stage;
        int stage = -1;
        int routingMode = 0;
    };''', "GraphNode routing mode")
text = replace_once(text,
'''    std::map<std::string, juce::Point<float>> restoredNodePositions;
    std::map<std::string, int> restoredEdgeSequences;''',
'''    std::map<std::string, juce::Point<float>> restoredNodePositions;
    std::map<std::string, int> restoredNodeRoutingModes;
    std::map<std::string, int> restoredEdgeSequences;''', "SignalLab restored routing map")
text = replace_once(text,
'''        restoredNodePositions.clear(); nodeOffsets.clear(); restoredEdgeSequences.clear();
        for (const auto& node : state.graph.nodes)
            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };
        for (const auto& edge : state.graph.edges)''',
'''        restoredNodePositions.clear(); nodeOffsets.clear(); restoredNodeRoutingModes.clear(); restoredEdgeSequences.clear();
        for (const auto& node : state.graph.nodes)
        {
            if (node.positionValid) restoredNodePositions[node.id.toStdString()] = { node.position.x, node.position.y };
            if (node.routingMode != 0) restoredNodeRoutingModes[node.id.toStdString()] = node.routingMode;
        }
        for (const auto& edge : state.graph.edges)''', "Undo restore routing map")

# Insert topology menu before fixed connection menu.
anchor = '''    void showFixedConnectionMenu()
    {'''
menu = r'''    void setFxTopologyMode (const GraphNode& node, int mode)
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
        juce::Component::SafePointer<SignalLabPage> safeThis (this);
        const auto id = node.id;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis, id] (int result)
        {
            if (safeThis == nullptr || (result != 100 && result != 101)) return;
            const int index = safeThis->findNodeIndex (id.toStdString());
            if (index >= 0) safeThis->setFxTopologyMode (safeThis->nodes[(size_t) index], result - 100);
        });
    }

'''
if anchor not in text: raise RuntimeError("FX topology menu anchor missing")
text = text.replace(anchor, menu + anchor, 1)

# addNode derives persisted routing mode and transfers it into the typed graph.
text = replace_once(text,
'''        node.inputPort = inputPort; node.outputPort = outputPort; node.role = role; node.stage = stage;
        node.modInputPort = modInputPort; node.modOutputPort = modOutputPort;

        PatchGraph::Node modelNode;''',
'''        node.inputPort = inputPort; node.outputPort = outputPort; node.role = role; node.stage = stage;
        node.modInputPort = modInputPort; node.modOutputPort = modOutputPort;
        if (const auto routing = restoredNodeRoutingModes.find (stableId); routing != restoredNodeRoutingModes.end())
            node.routingMode = juce::jlimit (0, 1, routing->second);

        PatchGraph::Node modelNode;''', "GraphNode derive routing mode")
text = replace_once(text,
'''        modelNode.position = { node.worldBounds.getX(), node.worldBounds.getY() };
        modelNode.positionValid = true;''',
'''        modelNode.position = { node.worldBounds.getX(), node.worldBounds.getY() };
        modelNode.positionValid = true;
        modelNode.routingMode = node.routingMode;''', "Graph model routing mode")

# FX detail text reflects persisted routing topology.
text = replace_once(text,
'''            const juce::String combineDetail = layer < 0 ? "MASTER START" : opNames[juce::jlimit (0, 4, op)] + " / " + juce::String (parameter ("layer" + juce::String (layer + 1) + "Amount", 1.0f), 2);
            const juce::String details[] { instanceDetail, "sources + tables", "algorithm + ops", "cutoff + ADSR", "pre + built-in + post", combineDetail };''',
'''            const juce::String combineDetail = layer < 0 ? "MASTER START" : opNames[juce::jlimit (0, 4, op)] + " / " + juce::String (parameter ("layer" + juce::String (layer + 1) + "Amount", 1.0f), 2);
            const auto fxKey = (prefix + ":S4").toStdString();
            const auto fxModeIt = restoredNodeRoutingModes.find (fxKey);
            const int fxMode = fxModeIt != restoredNodeRoutingModes.end() ? juce::jlimit (0, 1, fxModeIt->second) : 0;
            const juce::String fxDetail = fxMode == 1 ? "PARALLEL PRE || CORE > POST" : "SERIAL PRE > CORE > POST";
            const juce::String details[] { instanceDetail, "sources + tables", "algorithm + ops", "cutoff + ADSR", fxDetail, combineDetail };''', "FX node topology detail")

# Toolbar help mentions the new node action.
text = replace_once(text,
'''            ? "PATCH MAP / DRAG MOD = ADD / CABLE END = RECONNECT / DEL = REMOVE     CLOCK "''',
'''            ? "PATCH MAP / RIGHT-CLICK FX = SERIAL/PARALLEL / DRAG MOD = ADD / DEL = REMOVE     CLOCK "''', "Compact toolbar routing hint")
text = replace_once(text,
'''            : "PATCH MAP / DRAG MOD JACK = ADD / DRAG CABLE END = RECONNECT / DEL = REMOVE / CTRL-CMD+Z = UNDO    CLOCK: "''',
'''            : "PATCH MAP / RIGHT-CLICK FX = SERIAL/PARALLEL / DRAG MOD = ADD / DEL = REMOVE / CTRL-CMD+Z = UNDO    CLOCK: "''', "Toolbar routing hint")
write(path, text)

# -----------------------------------------------------------------------------
# Regression source: schema/atomic round-trip and audible finite parallel branch.
# -----------------------------------------------------------------------------
path = "Tests/SmokeTests.cpp"
text = read(path)
text = replace_once(text,
'''        if (! addAudioNode ("L0:S5", PatchGraph::NodeType::mixer, false, true)
            || ! addAudioNode ("L1:S5", PatchGraph::NodeType::mixer, false, true)''',
'''        PatchGraph::Node mainFx; mainFx.id = "L-1:S4"; mainFx.type = PatchGraph::NodeType::processor; mainFx.routingMode = 1;
        if (! graph.addNode (mainFx)
            || ! addAudioNode ("L0:S5", PatchGraph::NodeType::mixer, false, true)
            || ! addAudioNode ("L1:S5", PatchGraph::NodeType::mixer, false, true)''', "Routing fixture parallel FX node")
text = replace_once(text,
'''        if (! compiled.validation.ok || compiled.plan.layerOrder[0] != 1 || compiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler ignored persisted layer-combine order");''',
'''        if (! compiled.validation.ok || compiled.plan.layerOrder[0] != 1 || compiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler ignored persisted layer-combine order");
        if (! compiled.plan.parallelFx[0])
            return fail ("routing compiler ignored main FX parallel topology");''', "Routing compiler FX assertion")
text = replace_once(text,
'''        if (! restoredCompiled.validation.ok || restoredCompiled.plan.layerOrder[0] != 1 || restoredCompiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler order did not survive graph state round-trip");''',
'''        if (! restoredCompiled.validation.ok || restoredCompiled.plan.layerOrder[0] != 1 || restoredCompiled.plan.layerOrder[1] != 0)
            return fail ("routing compiler order did not survive graph state round-trip");
        if (! restoredCompiled.plan.parallelFx[0])
            return fail ("parallel FX topology did not survive graph state round-trip");''', "Routing roundtrip FX assertion")
text = replace_once(text,
'''        if (snapshot.layerOrder != restoredCompiled.plan.layerOrder || ! snapshot.graphAuthored)
            return fail ("atomic routing plan publication changed the compiled permutation");''',
'''        if (snapshot.layerOrder != restoredCompiled.plan.layerOrder || snapshot.parallelFx != restoredCompiled.plan.parallelFx || ! snapshot.graphAuthored)
            return fail ("atomic routing plan publication changed the compiled topology");''', "Atomic FX topology assertion")

# Add audible parallel regression after the existing pre/post FX routing check.
text = replace_once(text,
'''        tone.fxModules[0].stage = 1;
        if (maxDifference (pre, OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220)) < 0.001f)
            return fail ("pre/post FX routing had no effect");
        tone.fxModules = {}; tone.drive = 0;''',
'''        tone.fxModules[0].stage = 1;
        if (maxDifference (pre, OfflineRenderer::renderPatch (tone, 22050, 0.6f, 220)) < 0.001f)
            return fail ("pre/post FX routing had no effect");

        auto parallelProbe = tone;
        parallelProbe.fxModules[0] = { 1, 0, false, 0.42f, 0.25f, 0.10f, 1.0f };
        parallelProbe.fxModules[1] = { 3, 1, false, 0.52f, 0.55f, 0.40f, 0.85f };
        parallelProbe.drive = 0.48f; parallelProbe.chorusMix = 0.22f; parallelProbe.delayMix = 0.12f;
        DspRouting::Plan serialPlan, parallelPlan;
        parallelPlan.parallelFx[0] = true; parallelPlan.graphAuthored = true;
        const auto serialFx = OfflineRenderer::renderPatch (parallelProbe, 22050, 0.6f, 220, 128, serialPlan);
        const auto parallelFx = OfflineRenderer::renderPatch (parallelProbe, 22050, 0.6f, 220, 128, parallelPlan);
        if (! finiteAudio (parallelFx) || parallelFx.getMagnitude (0, parallelFx.getNumSamples()) <= 1.0e-5f)
            return fail ("parallel FX routing generated invalid or silent audio");
        if (maxDifference (serialFx, parallelFx) < 0.001f)
            return fail ("serial and compensated parallel FX routing sounded identical");
        if (parallelFx.getMagnitude (0, parallelFx.getNumSamples()) > serialFx.getMagnitude (0, serialFx.getNumSamples()) * 3.0f + 0.05f)
            return fail ("parallel FX split/merge produced an unsafe level jump");

        tone.fxModules = {}; tone.drive = 0;''', "Parallel FX DSP regression")
write(path, text)

# -----------------------------------------------------------------------------
# Plan progress.
# -----------------------------------------------------------------------------
path = "plan.md"
text = read(path)
replacements = {
'- [ ] Quick actions: **Insert after**, **Split parallel**, **Merge**, **Disconnect**, **Restore default route**.': '- [~] Quick actions: FX-node right-click now provides a DSP-backed **SERIAL / PARALLEL** split primitive; generic Insert/Merge/Disconnect/Restore actions remain pending.',
'- [ ] Parallel filter/FX branches with explicit split/merge gain compensation.': '- [~] Parallel FX branching is implemented per synth instance: PRE rack and built-in FX core split from the same dry input, merge at compensated 50/50 gain, then POST rack runs once. Arbitrary multi-node branches remain pending.',
'- [~] Preallocate node/process buffers in `prepareToPlay` or graph-plan preparation. Compiler v1 reuses the existing preallocated layer engines and `layerScratch`; general split/merge buffers are still pending.': '- [~] Preallocate node/process buffers in `prepareToPlay` or graph-plan preparation. Layer engines plus the new per-engine parallel-FX scratch are preallocated; arbitrary multi-node graph buffers remain pending.',
'- [ ] Add dry/wet and gain normalization around parallel branches to avoid surprise level jumps.': '- [~] Add dry/wet and gain normalization around parallel branches. The first FX split uses deterministic 0.5/0.5 correlated-unity compensation; configurable branch gain/dry-wet remains pending.',
'- [ ] Parallel split/merge loudness regression tests.': '- [~] Parallel split/merge loudness regression tests: the first FX routing regression verifies finite/non-silent output, audible serial-vs-parallel change and a bounded level jump; broader fixture coverage remains pending.',
'4. **DSP compiler v1** — safe serial/parallel audio routing and layer combine topology. *(layer-combine ordering + immutable atomic plan implemented; serial FX routing and parallel split/merge remain next)*': '4. **DSP compiler v2** — safe serial/parallel audio routing and layer combine topology. *(layer ordering + atomic plan + per-instance compensated FX parallel routing implemented; arbitrary branch utilities remain next)*'
}
for old, new in replacements.items():
    if old not in text: raise RuntimeError(f"plan item missing: {old[:80]}")
    text = text.replace(old, new, 1)
text = text.replace('- [x] Add graph schema versioning independent of plug-in version. Schema v2 adds optional layer-combine sequence metadata; v1/legacy edges default to canonical order.',
                    '- [x] Add graph schema versioning independent of plug-in version. Schema v3 adds optional processor `routingMode`; v2 adds layer-combine sequence metadata; older sessions default to canonical serial routing.', 1)
write(path, text)

print("DSP routing v2 parallel FX patch applied")
