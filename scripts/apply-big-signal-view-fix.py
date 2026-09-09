#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
signal = ROOT / "Source/UI/SignalLabPage.h"
plan = ROOT / "plan.md"
static = ROOT / "scripts/static-check.py"

text = signal.read_text(encoding="utf-8")

def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1 match, found {count}")
    text = text.replace(old, new, 1)

replace_once(
'''        setWantsKeyboardFocus (true);\n        startTimerHz (30);\n    }\n\n    void paint (juce::Graphics& g) override\n''',
'''        setWantsKeyboardFocus (true);\n        bigViewNeedsFit = mapOnlyMode;\n        startTimerHz (30);\n    }\n\n    void resized() override\n    {\n        if (mapOnlyMode)\n        {\n            bigViewNeedsFit = true;\n            repaint();\n        }\n    }\n\n    void paint (juce::Graphics& g) override\n''',
"constructor/resized")

replace_once(
'''    bool panning = false, draggingNode = false, snapToGrid = true, connecting = false;\n    bool restoringHistory = false, dragHistoryCaptured = false;\n''',
'''    bool panning = false, draggingNode = false, snapToGrid = true, connecting = false;\n    bool restoringHistory = false, dragHistoryCaptured = false, bigViewNeedsFit = false;\n''',
"big view fit member")

replace_once(
'''        const bool compactHeader = header.getHeight() > 40.0f;\n''',
'''        const bool compactHeader = mapOnlyMode || header.getHeight() > 40.0f;\n''',
"responsive toolbar")

replace_once(
'''        const float headerHeight = bounds.getWidth() < 980.0f ? 56.0f : 30.0f;\n''',
'''        const float headerHeight = mapOnlyMode ? 58.0f : (bounds.getWidth() < 980.0f ? 56.0f : 30.0f);\n''',
"large map header")

old_overlay = re.compile(r'''    void showPatchMapOverlay\(\)\n    \{\n        if \(mapOnlyMode\) return;\n        auto\* content = new SignalLabPage \(proc, true\);\n        content->setSize \(1320, 760\);\n        juce::DialogWindow::LaunchOptions options;\n        options\.content\.setOwned \(content\);\n        options\.dialogTitle = "RM-01 / LARGE PATCH MAP";\n        options\.dialogBackgroundColour = juce::Colour \(0xff101719\);\n        options\.escapeKeyTriggersCloseButton = true;\n        options\.useNativeTitleBar = false;\n        options\.resizable = true;\n        if \(auto\* dialogWindow = options\.launchAsync\(\)\)\n        \{\n            dialogWindow->setResizeLimits \(900, 540, 2400, 1600\);\n            dialogWindow->centreWithSize \(1320, 760\);\n        \}\n    \}\n''')
new_overlay = '''    void showPatchMapOverlay()\n    {\n        if (mapOnlyMode) return;\n\n        // LaunchOptions sizes the top-level window around a fixed-size content component.\n        // For the large map we need the opposite relationship: the SignalLabPage must follow\n        // every dialog resize so its toolbar/viewport never live outside the visible window.\n        auto* content = new SignalLabPage (proc, true);\n        auto* dialogWindow = new juce::DialogWindow ("RM-01 / LARGE PATCH MAP",\n                                                     juce::Colour (0xff101719), true, true);\n        dialogWindow->setUsingNativeTitleBar (false);\n        dialogWindow->setResizable (true, false);\n        dialogWindow->setResizeLimits (900, 540, 2400, 1600);\n        dialogWindow->setContentOwned (content, false);\n        dialogWindow->centreWithSize (1320, 760);\n        dialogWindow->setVisible (true);\n        dialogWindow->enterModalState (true, nullptr, true);\n    }\n'''
text, count = old_overlay.subn(new_overlay, text, count=1)
if count != 1:
    raise RuntimeError(f"overlay replacement: expected 1 match, found {count}")

old_fit = re.compile(r'''    void fitToView\(\)\n    \{\n        if \(nodes\.empty\(\) \|\| graphViewport\.isEmpty\(\)\)\n        \{\n            graphZoom = 0\.82f;\n            graphPan = \{ 18\.0f, 18\.0f \};\n            repaint\(\);\n            return;\n        \}\n        auto world = nodes\.front\(\)\.worldBounds;\n        for \(size_t i = 1; i < nodes\.size\(\); \+\+i\) world = world\.getUnion \(nodes\[i\]\.worldBounds\);\n        world = world\.expanded \(28\.0f\);\n        const float sx = graphViewport\.getWidth\(\) / juce::jmax \(1\.0f, world\.getWidth\(\)\);\n        const float sy = graphViewport\.getHeight\(\) / juce::jmax \(1\.0f, world\.getHeight\(\)\);\n        graphZoom = juce::jlimit \(kReadableGraphZoom, 1\.65f, juce::jmin \(sx, sy\)\);\n        graphPan = graphViewport\.getCentre\(\) - graphViewport\.getPosition\(\) - world\.getCentre\(\) \* graphZoom;\n        stageGraphStateForPersistence\(\);\n        repaint\(\);\n    \}\n''')
new_fit = '''    void applyFitToView()\n    {\n        if (nodes.empty() || graphViewport.isEmpty())\n        {\n            graphZoom = 0.82f;\n            graphPan = { 18.0f, 18.0f };\n            return;\n        }\n        auto world = nodes.front().worldBounds;\n        for (size_t i = 1; i < nodes.size(); ++i) world = world.getUnion (nodes[i].worldBounds);\n        world = world.expanded (28.0f);\n        const float sx = graphViewport.getWidth() / juce::jmax (1.0f, world.getWidth());\n        const float sy = graphViewport.getHeight() / juce::jmax (1.0f, world.getHeight());\n        graphZoom = juce::jlimit (kReadableGraphZoom, 1.65f, juce::jmin (sx, sy));\n        graphPan = graphViewport.getCentre() - graphViewport.getPosition() - world.getCentre() * graphZoom;\n    }\n\n    void fitToView()\n    {\n        applyFitToView();\n        stageGraphStateForPersistence();\n        repaint();\n    }\n'''
text, count = old_fit.subn(new_fit, text, count=1)
if count != 1:
    raise RuntimeError(f"fit refactor: expected 1 match, found {count}")

replace_once(
'''        const int clock = addNode ({ 930.0f, masterY + 64.0f, 126.0f, nodeH }, "CLOCK", "TEMPO CLOCK",\n                                   juce::String (proc.getEffectiveBpm(), 1) + " BPM", "MOD", -2, accent,\n                                   false, true, NodeRole::clock);\n        for (const auto mod : modNodes) connect (clock, mod, EdgeKind::clock);\n\n        drawToolbar (g, header, led, accent);\n''',
'''        const int clock = addNode ({ 930.0f, masterY + 64.0f, 126.0f, nodeH }, "CLOCK", "TEMPO CLOCK",\n                                   juce::String (proc.getEffectiveBpm(), 1) + " BPM", "MOD", -2, accent,\n                                   false, true, NodeRole::clock);\n        for (const auto mod : modNodes) connect (clock, mod, EdgeKind::clock);\n\n        if (mapOnlyMode && bigViewNeedsFit)\n        {\n            applyFitToView();\n            bigViewNeedsFit = false;\n        }\n\n        drawToolbar (g, header, led, accent);\n''',
"fit large map after topology build")

signal.write_text(text, encoding="utf-8")

plan_text = plan.read_text(encoding="utf-8")
needle = "- [x] Persist custom node positions, pan, zoom and grid preference in UI/session state.\n"
addition = needle + "- [x] LARGE PATCH MAP is genuinely resizable: content follows the window, the full toolbar stays visible, and the graph re-fits after overlay resize.\n"
if addition not in plan_text:
    if needle not in plan_text:
        raise RuntimeError("plan insertion point not found")
    plan_text = plan_text.replace(needle, addition, 1)
plan.write_text(plan_text, encoding="utf-8")

static_text = static.read_text(encoding="utf-8")
needle = "    'interactive signal map': ['mouseWheelMove', 'mouseDrag', 'mouseDoubleClick', 'graphZoom', 'graphPan', 'INSTANCE 1 / MAIN'],\n"
addition = needle + "    'responsive large signal map': ['bigViewNeedsFit', 'applyFitToView', 'setContentOwned (content, false)', 'mapOnlyMode || header.getHeight() > 40.0f'],\n"
if addition not in static_text:
    if needle not in static_text:
        raise RuntimeError("static token insertion point not found")
    static_text = static_text.replace(needle, addition, 1)
needle = "    'interactive signal map': signal_page,\n"
addition = needle + "    'responsive large signal map': signal_page,\n"
if addition not in static_text:
    if needle not in static_text:
        raise RuntimeError("static text mapping insertion point not found")
    static_text = static_text.replace(needle, addition, 1)
static.write_text(static_text, encoding="utf-8")

print("Applied responsive LARGE PATCH MAP fix")
