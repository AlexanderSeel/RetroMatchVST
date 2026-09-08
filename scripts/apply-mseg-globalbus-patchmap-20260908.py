from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if old not in text:
        raise RuntimeError(f'marker not found in {path}: {old[:120]!r}')
    if text.count(old) != 1:
        raise RuntimeError(f'marker not unique in {path}: {text.count(old)} occurrences')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')


def append_once(path, marker, text_to_add):
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if text_to_add.strip() in text:
        return
    if marker not in text:
        raise RuntimeError(f'append marker not found in {path}')
    p.write_text(text.replace(marker, marker + text_to_add, 1), encoding='utf-8')


# -----------------------------------------------------------------------------
# Global post-stack effects/filter bus.
# -----------------------------------------------------------------------------
replace_once('Source/PluginProcessor.h',
'''    SynthEngine engine;\n    juce::MidiBuffer renderMidi;''',
'''    SynthEngine engine;\n    // True whole-instrument bus: runs once after main + all layer instances are combined.\n    ModuleRack globalModuleRack;\n    juce::MidiBuffer renderMidi;''')

replace_once('Source/PluginProcessor.cpp',
'''    if (id.startsWith ("layer") || id == "mainLayerGain" || id == "oversamplingQuality" || id == "resynthInstances"\n        || id == "masterOutputGain" || id == "resynthStrategy" || id == "resynthComplexity")''',
'''    if (id.startsWith ("layer") || id.startsWith ("globalFxModule") || id == "mainLayerGain" || id == "oversamplingQuality" || id == "resynthInstances"\n        || id == "masterOutputGain" || id == "resynthStrategy" || id == "resynthComplexity")''')

replace_once('Source/PluginProcessor.cpp',
'''    engine.prepare (sr, bs, channels);\n    renderMidi.ensureSize (131072);''',
'''    engine.prepare (sr, bs, channels);\n    globalModuleRack.prepare (sr, bs, channels);\n    renderMidi.ensureSize (131072);''')

replace_once('Source/PluginProcessor.cpp',
'''    engine.setParameters (readParams());\n    engine.render (b, renderMidi);\n    if (mode == ReferenceAuditionMode::referenceOnly) b.clear();''',
'''    engine.setParameters (readParams());\n    engine.render (b, renderMidi);\n\n    // Whole-synth global bus. This happens after SynthEngine has rendered and combined\n    // every active instance, so filters/effects here colour the complete patch instead\n    // of being repeated separately inside each layer. Reference-only audition remains dry.\n    if (mode != ReferenceAuditionMode::referenceOnly)\n    {\n        std::array<FxModuleParameters, FxModuleParameters::slotCount> globalModules {};\n        for (int i = 0; i < FxModuleParameters::slotCount; ++i)\n        {\n            const auto prefix = "globalFxModule" + juce::String (i + 1);\n            auto value = [this, &prefix] (const char* suffix, float fallback)\n            {\n                if (auto* v = apvts.getRawParameterValue (prefix + suffix)) return v->load();\n                return fallback;\n            };\n            auto& module = globalModules[(size_t) i];\n            module.type = (int) value ("Type", 0.0f);\n            module.stage = (int) value ("Stage", 0.0f);\n            module.bypass = value ("Bypass", 0.0f) >= 0.5f;\n            module.amount = value ("Amount", 0.5f);\n            module.rate = value ("Rate", 0.25f);\n            module.feedback = value ("Feedback", 0.25f);\n            module.mix = value ("Mix", 0.5f);\n            module.tempoSync = value ("TempoSync", 0.0f) >= 0.5f;\n            module.tempoDivision = (int) value ("Division", 3.0f);\n        }\n        globalModuleRack.process (b, globalModules, 0, effectiveBpm.load (std::memory_order_relaxed));\n        globalModuleRack.process (b, globalModules, 1, effectiveBpm.load (std::memory_order_relaxed));\n    }\n\n    if (mode == ReferenceAuditionMode::referenceOnly) b.clear();''')

replace_once('Source/PluginProcessor.cpp',
'''    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)\n    {\n        const auto prefix = "fxModule" + juce::String (i);\n        setDefault (prefix + "Type", 0); setDefault (prefix + "Stage", 0); setDefault (prefix + "Bypass", false);\n        setDefault (prefix + "Amount", 0.5f); setDefault (prefix + "Rate", 0.25f); setDefault (prefix + "Feedback", 0.25f); setDefault (prefix + "Mix", 0.5f);\n        setDefault (prefix + "TempoSync", false); setDefault (prefix + "Division", 3);\n    }''',
'''    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)\n    {\n        const auto prefix = "fxModule" + juce::String (i);\n        setDefault (prefix + "Type", 0); setDefault (prefix + "Stage", 0); setDefault (prefix + "Bypass", false);\n        setDefault (prefix + "Amount", 0.5f); setDefault (prefix + "Rate", 0.25f); setDefault (prefix + "Feedback", 0.25f); setDefault (prefix + "Mix", 0.5f);\n        setDefault (prefix + "TempoSync", false); setDefault (prefix + "Division", 3);\n    }\n    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)\n    {\n        const auto prefix = "globalFxModule" + juce::String (i);\n        setDefault (prefix + "Type", 0); setDefault (prefix + "Stage", 0); setDefault (prefix + "Bypass", false);\n        setDefault (prefix + "Amount", 0.5f); setDefault (prefix + "Rate", 0.25f); setDefault (prefix + "Feedback", 0.25f); setDefault (prefix + "Mix", 0.5f);\n        setDefault (prefix + "TempoSync", false); setDefault (prefix + "Division", 3);\n    }''')

replace_once('Source/PluginProcessor.cpp',
'''    l.add (std::make_unique<P> ("masterOutputGain", "Master Output",\n        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f));\n    return l;''',
'''    l.add (std::make_unique<P> ("masterOutputGain", "Master Output",\n        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f));\n\n    // Append-only whole-synth bus parameters. Kept after all existing parameters so\n    // established automation indices remain stable. Each slot can be a filter or FX.\n    for (int i = 1; i <= FxModuleParameters::slotCount; ++i)\n    {\n        const auto prefix = "globalFxModule" + juce::String (i);\n        l.add (std::make_unique<C> (prefix + "Type", prefix + " Type", moduleTypes, 0));\n        l.add (std::make_unique<C> (prefix + "Stage", prefix + " Stage", juce::StringArray { "PRE", "POST" }, 0));\n        l.add (std::make_unique<B> (prefix + "Bypass", prefix + " Bypass", false));\n        l.add (std::make_unique<P> (prefix + "Amount", prefix + " Amount", juce::NormalisableRange<float> (0, 1), 0.5f));\n        l.add (std::make_unique<P> (prefix + "Rate", prefix + " Rate", juce::NormalisableRange<float> (0, 1), 0.25f));\n        l.add (std::make_unique<P> (prefix + "Feedback", prefix + " Feedback", juce::NormalisableRange<float> (0, 1), 0.25f));\n        l.add (std::make_unique<P> (prefix + "Mix", prefix + " Mix", juce::NormalisableRange<float> (0, 1), 0.5f));\n        l.add (std::make_unique<B> (prefix + "TempoSync", prefix + " Tempo Sync", false));\n        l.add (std::make_unique<C> (prefix + "Division", prefix + " Division", divisions, 3));\n    }\n    return l;''')

# -----------------------------------------------------------------------------
# FX page: reuse the proven modular rack UI for an independent GLOBAL BUS tab.
# -----------------------------------------------------------------------------
replace_once('Source/UI/FxRackPage.h',
'''    explicit ModularFxPage (RetroMatchSynthAudioProcessor& p) : proc (p)\n    {''',
'''    ModularFxPage (RetroMatchSynthAudioProcessor& p, juce::String prefix = "fxModule", juce::String title = "INSTANCE")\n        : proc (p), parameterPrefix (std::move (prefix)), rackTitle (std::move (title))\n    {''')

replace_once('Source/UI/FxRackPage.h',
'''            const auto prefix = "fxModule" + juce::String (i + 1);''',
'''            const auto prefix = parameterPrefix + juce::String (i + 1);''')

replace_once('Source/UI/FxRackPage.h',
'''        juce::String chain = "SYNTH  >  PRE: ";''',
'''        juce::String chain = rackTitle + "  >  PRE: ";''')

replace_once('Source/UI/FxRackPage.h',
'''    RetroMatchSynthAudioProcessor& proc;\n    struct Row''',
'''    RetroMatchSynthAudioProcessor& proc;\n    juce::String parameterPrefix, rackTitle;\n    struct Row''')

replace_once('Source/UI/FxRackPage.h',
'''    float value (int i, const char* key) const { return proc.apvts.getRawParameterValue ("fxModule" + juce::String (i + 1) + key)->load(); }\n    void set (int i, const char* key, float v) { auto* p = proc.apvts.getParameter ("fxModule" + juce::String (i + 1) + key); p->setValueNotifyingHost (p->convertTo0to1 (v)); }''',
'''    float value (int i, const char* key) const { return proc.apvts.getRawParameterValue (parameterPrefix + juce::String (i + 1) + key)->load(); }\n    void set (int i, const char* key, float v) { auto* p = proc.apvts.getParameter (parameterPrefix + juce::String (i + 1) + key); p->setValueNotifyingHost (p->convertTo0to1 (v)); }''')

replace_once('Source/UI/FxRackPage.h',
'''    FxRackPage (RetroMatchSynthAudioProcessor& p, juce::Component* builtIn)\n        : tempo (p), chorusSync (p, "chorusSync", "chorusDivision", "CHORUS"), delaySync (p, "delaySync", "delayDivision", "DELAY"), rack (p)\n    {\n        addAndMakeVisible (tempo); addAndMakeVisible (chorusSync); addAndMakeVisible (delaySync); addAndMakeVisible (tabs);\n        tabs.addTab ("MODULE RACK", juce::Colour (0xff101719), &rack, false);\n        tabs.addTab ("BUILT-IN FX", juce::Colour (0xff101719), builtIn, false);\n    }''',
'''    FxRackPage (RetroMatchSynthAudioProcessor& p, juce::Component* builtIn)\n        : tempo (p), chorusSync (p, "chorusSync", "chorusDivision", "CHORUS"), delaySync (p, "delaySync", "delayDivision", "DELAY"),\n          globalRack (p, "globalFxModule", "GLOBAL BUS"), rack (p, "fxModule", "INSTANCE")\n    {\n        addAndMakeVisible (tempo); addAndMakeVisible (chorusSync); addAndMakeVisible (delaySync); addAndMakeVisible (tabs);\n        tabs.addTab ("GLOBAL BUS", juce::Colour (0xff101719), &globalRack, false);\n        tabs.addTab ("INSTANCE RACK", juce::Colour (0xff101719), &rack, false);\n        tabs.addTab ("BUILT-IN FX", juce::Colour (0xff101719), builtIn, false);\n    }''')

replace_once('Source/UI/FxRackPage.h',
'''    ModularFxPage rack;\n    juce::TabbedComponent tabs''',
'''    ModularFxPage globalRack, rack;\n    juce::TabbedComponent tabs''')

# -----------------------------------------------------------------------------
# Patch Map: easy cable editing + large overlay + visible global bus.
# -----------------------------------------------------------------------------
replace_once('Source/UI/SignalLabPage.h',
'''    explicit SignalLabPage (RetroMatchSynthAudioProcessor& p) : proc (p)\n    {\n        setWantsKeyboardFocus (true);\n        startTimerHz (30);\n    }''',
'''    explicit SignalLabPage (RetroMatchSynthAudioProcessor& p, bool mapOnly = false) : proc (p), mapOnlyMode (mapOnly)\n    {\n        setWantsKeyboardFocus (true);\n        startTimerHz (30);\n    }''')

replace_once('Source/UI/SignalLabPage.h',
'''        auto area = getLocalBounds().toFloat().reduced (16);\n        const auto led = findColour (RetroLookAndFeel::primaryLed), accent = findColour (RetroLookAndFeel::secondaryLed);\n        g.setColour (led); g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));\n        g.drawText ("SIGNAL LAB  /  LIVE OUTPUT + WHOLE-SYNTH PATCH MAP", area.removeFromTop (32), juce::Justification::centredLeft);\n\n        auto displays = area.removeFromTop (area.getHeight() * 0.38f);''',
'''        auto area = getLocalBounds().toFloat().reduced (mapOnlyMode ? 8.0f : 16.0f);\n        const auto led = findColour (RetroLookAndFeel::primaryLed), accent = findColour (RetroLookAndFeel::secondaryLed);\n        if (mapOnlyMode)\n        {\n            drawPatchMap (g, area, led, accent);\n            return;\n        }\n        g.setColour (led); g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));\n        g.drawText ("SIGNAL LAB  /  LIVE OUTPUT + WHOLE-SYNTH PATCH MAP", area.removeFromTop (32), juce::Justification::centredLeft);\n\n        auto displays = area.removeFromTop (area.getHeight() * 0.38f);''')

replace_once('Source/UI/SignalLabPage.h',
'''        if (! e.mods.isMiddleButtonDown())\n        {\n            if (const int output = hitTestModOutput (e.position); output >= 0)''',
'''        if (! e.mods.isMiddleButtonDown())\n        {\n            // Editable cables are deliberately left-clickable as well as right-clickable.\n            // This removes the hidden-context-menu feel of the first patch-map version.\n            if (const int edge = hitTestEdge (e.position); edge >= 0 && edges[(size_t) edge].editKind != EdgeEditKind::none)\n            {\n                selectedEdgeKey = edges[(size_t) edge].key.toStdString();\n                showEdgeMenu (edges[(size_t) edge]);\n                repaint();\n                return;\n            }\n            if (const int output = hitTestModOutput (e.position); output >= 0)''')

replace_once('Source/UI/SignalLabPage.h',
'''    enum class ToolbarAction { none, autoArrange, fit, zoomOut, zoom100, zoomIn, grid };''',
'''    enum class ToolbarAction { none, autoArrange, fit, zoomOut, zoom100, zoomIn, grid, expand };''')

replace_once('Source/UI/SignalLabPage.h',
'''    RetroMatchSynthAudioProcessor& proc;\n    static constexpr int size = 2048;''',
'''    RetroMatchSynthAudioProcessor& proc;\n    bool mapOnlyMode = false;\n    static constexpr int size = 2048;''')

replace_once('Source/UI/SignalLabPage.h',
'''            case ToolbarAction::grid: snapToGrid = ! snapToGrid; repaint(); break;\n            case ToolbarAction::none: break;''',
'''            case ToolbarAction::grid: snapToGrid = ! snapToGrid; repaint(); break;\n            case ToolbarAction::expand: showPatchMapOverlay(); break;\n            case ToolbarAction::none: break;''')

replace_once('Source/UI/SignalLabPage.h',
'''    void setZoomAround (juce::Point<float> screenPoint, float requestedZoom)''',
'''    void showPatchMapOverlay()\n    {\n        if (mapOnlyMode) return;\n        auto* content = new SignalLabPage (proc, true);\n        content->setSize (1320, 760);\n        juce::DialogWindow::LaunchOptions options;\n        options.content.setOwned (content);\n        options.dialogTitle = "RM-01 / LARGE PATCH MAP";\n        options.dialogBackgroundColour = juce::Colour (0xff101719);\n        options.escapeKeyTriggersCloseButton = true;\n        options.useNativeTitleBar = false;\n        options.resizable = true;\n        if (auto* window = options.launchAsync())\n        {\n            window->setResizeLimits (900, 540, 2400, 1600);\n            window->centreWithSize (1320, 760);\n        }\n    }\n\n    void setZoomAround (juce::Point<float> screenPoint, float requestedZoom)''')

replace_once('Source/UI/SignalLabPage.h',
'''            juce::PathStrokeType (juce::jmax (8.0f, 10.0f * graphZoom)).createStrokedPath (hitArea, edgePath (edges[(size_t) i]));''',
'''            juce::PathStrokeType (juce::jmax (14.0f, 16.0f * graphZoom)).createStrokedPath (hitArea, edgePath (edges[(size_t) i]));''')

replace_once('Source/UI/SignalLabPage.h',
'''        const Def defs[] {{ "AUTO", 48, ToolbarAction::autoArrange }, { "FIT", 40, ToolbarAction::fit },\n                          { "-", 26, ToolbarAction::zoomOut }, { "100%", 42, ToolbarAction::zoom100 },\n                          { "+", 26, ToolbarAction::zoomIn }, { "GRID", 48, ToolbarAction::grid }};''',
'''        const Def defs[] {{ "BIG", 42, ToolbarAction::expand }, { "AUTO", 48, ToolbarAction::autoArrange }, { "FIT", 40, ToolbarAction::fit },\n                          { "-", 26, ToolbarAction::zoomOut }, { "100%", 42, ToolbarAction::zoom100 },\n                          { "+", 26, ToolbarAction::zoomIn }, { "GRID", 48, ToolbarAction::grid }};''')

replace_once('Source/UI/SignalLabPage.h',
'''        g.drawFittedText ("PATCH MAP / DRAG MOD JACK > VALID DESTINATION TO ADD / RIGHT-CLICK CABLE TO EDIT / FIXED AUDIO ORDER STAYS SAFE    CLOCK: "''',
'''        g.drawFittedText ("PATCH MAP / LEFT-CLICK EDITABLE CABLE / DRAG MOD JACK TO ADD / BIG = LARGE OVERLAY / FIXED AUDIO ORDER STAYS SAFE    CLOCK: "''')

replace_once('Source/UI/SignalLabPage.h',
'''        const int master = addNode ({ 770.0f, masterY - nodeH * 0.5f, 126.0f, nodeH }, "MASTER", "MASTER OUT",\n                                    juce::String (parameter ("masterOutputGain", 0.0f), 1) + " dB", "FX", -2, led,\n                                    true, false, NodeRole::master);\n        for (size_t i = 0; i < combineNodes.size(); ++i)\n        {\n            const int layer = combineLayers[i];\n            if (layer < 0)\n                connect (combineNodes[i], master, EdgeKind::audio);\n            else\n            {\n                const auto prefix = "layer" + juce::String (layer + 1);\n                const int operation = juce::jlimit (0, 4, (int) parameter (prefix + "Operation", 0.0f));\n                const juce::String opNames[] { "ADD", "MIX", "SUB", "MULT", "DIV" };\n                connect (combineNodes[i], master, EdgeKind::audio, EdgeEditKind::layerCombine, layer, -1,\n                         "combine:" + juce::String (layer), opNames[operation] + " " + juce::String (parameter (prefix + "Amount", 1.0f), 2));\n            }\n        }\n\n        const int clock = addNode ({ 770.0f, masterY + 64.0f, 126.0f, nodeH }, "CLOCK", "TEMPO CLOCK",''',
'''        int activeGlobalFx = 0;\n        for (int i = 1; i <= FxModuleParameters::slotCount; ++i)\n            if ((int) parameter ("globalFxModule" + juce::String (i) + "Type", 0.0f) > 0\n                && parameter ("globalFxModule" + juce::String (i) + "Bypass", 0.0f) < 0.5f) ++activeGlobalFx;\n        const int globalBus = addNode ({ 760.0f, masterY - nodeH * 0.5f, 132.0f, nodeH }, "GLOBALBUS", "GLOBAL FILTER / FX",\n                                       juce::String (activeGlobalFx) + " ACTIVE / WHOLE MIX", "FX", -2, accent, true, true, NodeRole::stage);\n        const int master = addNode ({ 930.0f, masterY - nodeH * 0.5f, 126.0f, nodeH }, "MASTER", "MASTER OUT",\n                                    juce::String (parameter ("masterOutputGain", 0.0f), 1) + " dB", "FX", -2, led,\n                                    true, false, NodeRole::master);\n        for (size_t i = 0; i < combineNodes.size(); ++i)\n        {\n            const int layer = combineLayers[i];\n            if (layer < 0)\n                connect (combineNodes[i], globalBus, EdgeKind::audio);\n            else\n            {\n                const auto prefix = "layer" + juce::String (layer + 1);\n                const int operation = juce::jlimit (0, 4, (int) parameter (prefix + "Operation", 0.0f));\n                const juce::String opNames[] { "ADD", "MIX", "SUB", "MULT", "DIV" };\n                connect (combineNodes[i], globalBus, EdgeKind::audio, EdgeEditKind::layerCombine, layer, -1,\n                         "combine:" + juce::String (layer), "EDIT / " + opNames[operation] + " " + juce::String (parameter (prefix + "Amount", 1.0f), 2));\n            }\n        }\n        connect (globalBus, master, EdgeKind::audio);\n\n        const int clock = addNode ({ 930.0f, masterY + 64.0f, 126.0f, nodeH }, "CLOCK", "TEMPO CLOCK",''')

# -----------------------------------------------------------------------------
# MSEG becomes a first-class resynthesis and factory-preset tool.
# -----------------------------------------------------------------------------
replace_once('Source/Matching/SoundMatcher.cpp',
'''    p.release = juce::jlimit (0.03f, 5.0f, f.releaseSeconds > 0.01f ? f.releaseSeconds : (f.duration < 1.0f ? 0.18f : 0.55f));\n\n    p.cutoff =''',
'''    p.release = juce::jlimit (0.03f, 5.0f, f.releaseSeconds > 0.01f ? f.releaseSeconds : (f.duration < 1.0f ? 0.18f : 0.55f));\n\n    // MSEG is valuable whenever the target contains a meaningful transient or\n    // time-varying spectrum. Seed it from measured envelope/motion instead of\n    // leaving the optimizer to discover the topology by chance.\n    const bool transientMseg = f.transientScore > 0.28f;\n    const bool movingMseg = f.spectralMotion > 0.045f;\n    const bool evolvingMseg = f.duration > 1.4f && f.sustainLevel > 0.18f;\n    p.mseg.enabled = transientMseg || movingMseg || evolvingMseg;\n    if (p.mseg.enabled)\n    {\n        p.msegTarget = transientMseg ? (int) ModDestination::amplitude\n                                    : (movingMseg ? (int) ModDestination::wavetablePosition : (int) ModDestination::cutoff);\n        p.msegDepth = transientMseg ? 0.92f : juce::jlimit (0.22f, 0.72f, 0.28f + f.spectralMotion * 2.2f);\n        p.mseg.levels = {{ 0.0f, 1.0f, juce::jlimit (0.18f, 0.92f, f.sustainLevel + 0.16f),\n                           juce::jlimit (0.10f, 0.86f, f.sustainLevel),\n                           juce::jlimit (0.06f, 0.78f, f.sustainLevel * 0.78f), 0.0f }};\n        p.mseg.times = {{ juce::jlimit (0.002f, 2.5f, p.attack), juce::jlimit (0.008f, 2.5f, p.decay * 0.42f),\n                          juce::jlimit (0.015f, 3.5f, p.decay * 0.75f), juce::jlimit (0.025f, 5.0f, f.duration * 0.18f),\n                          juce::jlimit (0.025f, 5.0f, p.release) }};\n        p.mseg.curves = {{ transientMseg ? -0.34f : 0.10f, 0.18f, -0.08f, movingMseg ? 0.28f : 0.08f, -0.22f }};\n        p.mseg.loopEnabled = ! transientMseg && (movingMseg || evolvingMseg);\n        p.mseg.loopStartPoint = 1; p.mseg.loopEndPoint = 4;\n        if (transientMseg && movingMseg)\n            p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff, 0.28f };\n    }\n\n    p.cutoff =''')

replace_once('Source/Matching/SoundMatcher.cpp',
'''                if (reference.spectralMotion > 0.06f)\n                    p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition,\n                                           juce::jlimit (0.08f, 0.42f, reference.spectralMotion * 1.8f) };''',
'''                if (reference.spectralMotion > 0.06f)\n                {\n                    p.mseg.enabled = true; p.mseg.loopEnabled = true;\n                    p.msegTarget = (int) ModDestination::wavetablePosition;\n                    p.msegDepth = juce::jlimit (0.22f, 0.62f, reference.spectralMotion * 2.2f);\n                    p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff,\n                                           juce::jlimit (0.08f, 0.32f, reference.spectralMotion * 1.35f) };\n                }''')

replace_once('Source/Matching/SoundMatcher.cpp',
'''            if (reference.spectralMotion > 0.05f)\n                p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition,\n                                       juce::jlimit (0.06f, 0.32f, reference.spectralMotion * 1.25f) };''',
'''            if (reference.spectralMotion > 0.05f)\n            {\n                p.mseg.enabled = true; p.mseg.loopEnabled = true; p.msegTarget = (int) ModDestination::cutoff;\n                p.msegDepth = juce::jlimit (0.18f, 0.58f, 0.20f + reference.spectralMotion * 1.7f);\n                p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::wavetablePosition,\n                                       juce::jlimit (0.08f, 0.38f, reference.spectralMotion * 1.45f) };\n            }''')

replace_once('Source/Matching/SoundMatcher.cpp',
'''            p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition,\n                                   juce::jlimit (0.12f, 0.52f, 0.16f + motion * 0.38f) };\n            p.extraLfoRate[0] = juce::jlimit (0.05f, 1.8f, 0.10f + reference.spectralMotion * 2.4f);''',
'''            p.mseg.enabled = true; p.mseg.loopEnabled = true; p.msegTarget = (int) ModDestination::wavetablePosition;\n            p.msegDepth = juce::jlimit (0.26f, 0.72f, 0.30f + motion * 0.36f);\n            p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::wavefold,\n                                   juce::jlimit (0.10f, 0.42f, 0.12f + motion * 0.30f) };\n            p.extraLfoRate[0] = juce::jlimit (0.05f, 1.8f, 0.10f + reference.spectralMotion * 2.4f);''')

replace_once('Source/Matching/SoundMatcher.cpp',
'''    if (allowTopology && random.nextFloat() < 0.12f) p.mseg.enabled = ! p.mseg.enabled;\n    if (p.mseg.enabled)\n    {\n        for (auto& level : p.mseg.levels) level = mutateLinear (level, 0, 1, amount, random);\n        for (auto& time : p.mseg.times) time = mutateLog (time, 0.001f, 4, amount, random);\n        for (auto& curve : p.mseg.curves) curve = mutateLinear (curve, -1, 1, amount, random);\n    }''',
'''    if (allowTopology && random.nextFloat() < 0.12f) p.mseg.enabled = ! p.mseg.enabled;\n    if (p.mseg.enabled)\n    {\n        p.msegDepth = mutateLinear (p.msegDepth, -1.0f, 1.0f, amount * 0.70f, random);\n        if (allowTopology && random.nextFloat() < 0.14f)\n            p.msegTarget = random.nextInt ((int) ModDestination::wavefold + 1);\n        if (allowTopology && random.nextFloat() < 0.09f) p.mseg.loopEnabled = ! p.mseg.loopEnabled;\n        for (auto& level : p.mseg.levels) level = mutateLinear (level, 0, 1, amount, random);\n        for (auto& time : p.mseg.times) time = mutateLog (time, 0.001f, 4, amount, random);\n        for (auto& curve : p.mseg.curves) curve = mutateLinear (curve, -1, 1, amount, random);\n    }''')

replace_once('Source/PluginProcessor.cpp',
'''        case 3: // Motion table.\n            if (table) p.referenceWavetableMix = juce::jmax (0.52f, p.referenceWavetableMix);\n            p.wavetableMix = juce::jmax (0.16f, p.wavetableMix);\n            p.extraLfoRate[0] = 0.11f;\n            p.modGraphSlots[0] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition, 0.36f };\n            p.chorusMix = juce::jmax (0.10f, p.chorusMix); p.stereoWidth = juce::jmax (1.2f, p.stereoWidth);\n            break;''',
'''        case 3: // Motion table: let MSEG carry the long-form motion, with LFO as a secondary shimmer.\n            if (table) p.referenceWavetableMix = juce::jmax (0.52f, p.referenceWavetableMix);\n            p.wavetableMix = juce::jmax (0.16f, p.wavetableMix);\n            p.mseg.enabled = true; p.mseg.loopEnabled = true; p.msegTarget = (int) ModDestination::wavetablePosition; p.msegDepth = 0.48f;\n            p.mseg.levels = {{ 0.18f, 0.82f, 0.46f, 0.94f, 0.35f, 0.62f }};\n            p.mseg.times = {{ 0.18f, 0.46f, 0.72f, 0.54f, 0.90f }};\n            p.mseg.curves = {{ -0.15f, 0.24f, -0.22f, 0.18f, -0.08f }};\n            p.extraLfoRate[0] = 0.11f;\n            p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff, 0.24f };\n            p.modGraphSlots[1] = { (int) ModSource::lfo2, (int) ModDestination::wavetablePosition, 0.12f };\n            p.chorusMix = juce::jmax (0.10f, p.chorusMix); p.stereoWidth = juce::jmax (1.2f, p.stereoWidth);\n            break;''')

replace_once('Source/Engine/PresetLibrary.h',
'''        p.moduleModSlots[1] = { (int) ModSource::lfo2, (int) ModDestination::cutoff, 0.08f + t * 0.2f };\n        if (family == 7)''',
'''        p.moduleModSlots[1] = { (int) ModSource::lfo2, (int) ModDestination::cutoff, 0.08f + t * 0.2f };\n        if (variation >= 2 && (family == 2 || family == 3 || family == 4 || family == 5 || family == 6 || family == 8 || family == 9))\n        {\n            p.mseg.enabled = true;\n            p.mseg.loopEnabled = family == 3 || family == 4 || family == 5 || family == 8 || family == 9;\n            p.msegTarget = family == 2 || family == 4 ? (int) ModDestination::amplitude\n                          : (family == 3 || family == 8 || family == 9 ? (int) ModDestination::wavetablePosition : (int) ModDestination::cutoff);\n            p.msegDepth = family == 2 ? 0.92f : 0.28f + 0.36f * t;\n            p.mseg.levels = family == 2\n                ? std::array<float, MsegParameters::pointCount> {{ 0.0f, 1.0f, 0.52f, 0.24f, 0.10f, 0.0f }}\n                : std::array<float, MsegParameters::pointCount> {{ 0.16f, 0.86f, 0.42f, 0.92f, 0.34f, 0.62f }};\n            p.mseg.times = {{ 0.015f + t * 0.08f, 0.08f + t * 0.24f, 0.16f + t * 0.48f, 0.28f + t * 0.72f, 0.22f + t * 0.64f }};\n            p.mseg.curves = {{ -0.24f, 0.16f, -0.18f, 0.24f, -0.12f }};\n            if (p.msegTarget != (int) ModDestination::cutoff)\n                p.modGraphSlots[0] = { (int) ModSource::mseg1, (int) ModDestination::cutoff, 0.12f + 0.18f * t };\n        }\n        if (family == 7)''')

replace_once('Source/Engine/PresetLibrary.h',
'''                + "Voiced with tuned oscillators, envelope movement and complementary spatial effects."\n                + (variation >= 5 ? " Layer controls shape the ordered combination; edit each instance independently." : "") });''',
'''                + "Voiced with tuned oscillators, envelope movement and complementary spatial effects."\n                + (variation >= 2 && (family == 2 || family == 3 || family == 4 || family == 5 || family == 6 || family == 8 || family == 9) ? " MSEG motion is part of the authored sound." : "")\n                + (variation >= 5 ? " Layer controls shape the ordered combination; edit each instance independently." : "") });''')

# Document the implementation block so future work continues from the actual architecture.
append_once('plan.md', '\n', '''\n## MSEG + global bus + large Patch Map implementation block\n\n- MSEG is treated as a primary resynthesis dimension: the matcher seeds MSEG from transient/spectral-motion analysis and mutates target, depth, loop and shape during refinement.\n- Factory families with meaningful motion now intentionally author MSEG rather than relying only on LFOs.\n- The FX page exposes a GLOBAL BUS rack using the same filter/effect catalog as instance racks. It processes once after all active synth instances are combined.\n- Patch Map shows the global bus explicitly, exposes editable cables by normal left-click, increases cable hit targets and provides a BIG overlay view.\n- Next matching milestone: advisor-driven method/depth ranking, heterogeneous method search, complete multilayer scoring and excitation-probe FX-chain reconstruction.\n''')

print('MSEG/global bus/patch-map migration applied')
