from pathlib import Path
import re


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, text):
    Path(path).write_text(text, encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


# Clang portability: std::size is declared by <iterator> in C++17/C++20.
path = "Source/PluginProcessor.cpp"
text = read(path)
if "#include <iterator>" not in text:
    text = replace_once(text, "#include <cmath>\n#include <vector>", "#include <cmath>\n#include <iterator>\n#include <vector>", "PluginProcessor iterator include")
write(path, text)

# Signal Lab / Patch Map: preserve readable controls and labels at small editor sizes.
path = "Source/UI/SignalLabPage.h"
text = read(path)
if "#include <iterator>" not in text:
    text = replace_once(text, "#include <cmath>\n#include <map>", "#include <cmath>\n#include <iterator>\n#include <map>", "SignalLab iterator include")

if "kReadableGraphZoom" not in text:
    text = replace_once(
        text,
        "class SignalLabPage final : public juce::Component, private juce::Timer\n{\npublic:",
        "class SignalLabPage final : public juce::Component, private juce::Timer\n{\n    static constexpr float kReadableGraphZoom = 0.50f;\n\npublic:",
        "SignalLab readable zoom constant")

text = replace_once(text, "graphZoom = restored.view.zoom;", "graphZoom = juce::jlimit (kReadableGraphZoom, 2.2f, restored.view.zoom);", "restored graph zoom clamp")
text = text.replace("juce::jlimit (0.35f, 2.2f, requestedZoom)", "juce::jlimit (kReadableGraphZoom, 2.2f, requestedZoom)")
text = text.replace("juce::jlimit (0.35f, 1.65f, juce::jmin (sx, sy))", "juce::jlimit (kReadableGraphZoom, 1.65f, juce::jmin (sx, sy))")

old_node = '''        g.setColour (node.colour); g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 10.0f * graphZoom), juce::Font::bold)));
        g.drawFittedText (node.title, r.reduced (6 * graphZoom, 2 * graphZoom).removeFromTop (r.getHeight() * 0.53f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (accent.withAlpha (0.82f)); g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 8.5f * graphZoom))));
        g.drawFittedText (node.detail, r.reduced (6 * graphZoom, 2 * graphZoom).withTrimmedTop (r.getHeight() * 0.48f).toNearestInt(), juce::Justification::centredLeft, 1);

        const float portRadius = juce::jmax (2.3f, 3.1f * graphZoom);'''
new_node = '''        const bool showDetail = graphZoom >= 0.62f && r.getWidth() >= 72.0f && r.getHeight() >= 27.0f;
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

        const float portRadius = juce::jmax (3.0f, 3.1f * graphZoom);'''
text = replace_once(text, old_node, new_node, "SignalLab node readability")

text = replace_once(text,
                    "if (edge.editKind == EdgeEditKind::layerCombine) drawEdgeLabel (g, edge, accent);",
                    "if (edge.editKind == EdgeEditKind::layerCombine && (selected || graphZoom >= 0.62f)) drawEdgeLabel (g, edge, accent);",
                    "layer edge label zoom guard")
text = replace_once(text,
                    "            drawEdgeLabel (g, edge, led);",
                    "            if (selected || graphZoom >= 0.62f) drawEdgeLabel (g, edge, led);",
                    "mod edge label zoom guard")

pattern = re.compile(r"    void drawToolbar \(juce::Graphics& g, juce::Rectangle<float> header, juce::Colour led, juce::Colour accent\)\n    \{.*?\n    \}\n\n    void drawPatchMap", re.S)
replacement = r'''    void drawToolbar (juce::Graphics& g, juce::Rectangle<float> header, juce::Colour led, juce::Colour accent)
    {
        toolbarButtons.clear();
        const bool compactHeader = header.getHeight() > 40.0f;
        const bool veryNarrow = header.getWidth() < 620.0f;
        const float gap = veryNarrow ? 3.0f : 4.0f;
        const float h = compactHeader ? 22.0f : 20.0f;
        struct Def { const char* label; float width; ToolbarAction action; };
        const Def defs[] {{ "BIG", 42, ToolbarAction::expand }, { "UNDO", 48, ToolbarAction::undo }, { "REDO", 48, ToolbarAction::redo },
                          { "AUTO", 48, ToolbarAction::autoArrange }, { "FIT", 40, ToolbarAction::fit }, { "-", 26, ToolbarAction::zoomOut },
                          { "100%", 42, ToolbarAction::zoom100 }, { "+", 26, ToolbarAction::zoomIn }, { "GRID", 48, ToolbarAction::grid }};

        float total = -gap;
        for (const auto& d : defs)
            if (! (mapOnlyMode && d.action == ToolbarAction::expand)) total += (veryNarrow ? juce::jmax (24.0f, d.width - 6.0f) : d.width) + gap;

        auto buttonRow = compactHeader ? header.removeFromBottom (26.0f) : header;
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
            ? "PATCH MAP / DRAG MOD = ADD / CABLE END = RECONNECT / DEL = REMOVE     CLOCK "
                + juce::String (proc.getEffectiveBpm(), 1) + " " + (daw ? "DAW" : "MANUAL")
            : "PATCH MAP / DRAG MOD JACK = ADD / DRAG CABLE END = RECONNECT / DEL = REMOVE / CTRL-CMD+Z = UNDO    CLOCK: "
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

    void drawPatchMap'''
text, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise RuntimeError(f"SignalLab toolbar replacement: expected one match, found {count}")

text = replace_once(text,
                    "        auto header = bounds.removeFromTop (30);\n        graphViewport = bounds.reduced (4);",
                    "        const float headerHeight = bounds.getWidth() < 980.0f ? 56.0f : 30.0f;\n        auto header = bounds.removeFromTop (headerHeight);\n        graphViewport = bounds.reduced (4);",
                    "SignalLab responsive header height")
write(path, text)

# MSEG: make fixed-width rows proportional so labels/controls do not collapse or overlap.
path = "Source/UI/MsegPage.h"
text = read(path)
old_header = '''        auto header = area.removeFromTop (68);
        auto enableRow = header.removeFromTop (32);
        enabled.setBounds (enableRow.removeFromLeft (150).reduced (2, 2));
        loopEnabled.setBounds (enableRow.removeFromLeft (190).reduced (2, 2));
        loopStartLabel.setBounds (enableRow.removeFromLeft (78));
        loopStart.setBounds (enableRow.removeFromLeft (82).reduced (2, 2));
        loopEndLabel.setBounds (enableRow.removeFromLeft (72));
        loopEnd.setBounds (enableRow.removeFromLeft (82).reduced (2, 2));
        auto directRow = header.removeFromTop (32);
        targetLabel.setBounds (directRow.removeFromLeft (95));
        target.setBounds (directRow.removeFromLeft (250).reduced (2, 2));
        directRow.removeFromLeft (10);
        depthLabel.setBounds (directRow.removeFromLeft (55));
        depth.setBounds (directRow.removeFromLeft (300).reduced (2, 2));'''
new_header = '''        auto header = area.removeFromTop (68);
        auto enableRow = header.removeFromTop (32);
        const int enabledWidth = juce::jlimit (100, 150, enableRow.getWidth() / 5);
        const int loopToggleWidth = juce::jlimit (120, 190, enableRow.getWidth() / 4);
        enabled.setBounds (enableRow.removeFromLeft (enabledWidth).reduced (2, 2));
        loopEnabled.setBounds (enableRow.removeFromLeft (loopToggleWidth).reduced (2, 2));
        const int loopGroupWidth = juce::jmax (1, enableRow.getWidth() / 2);
        auto loopStartArea = enableRow.removeFromLeft (loopGroupWidth);
        auto loopEndArea = enableRow;
        const int loopLabelWidth = juce::jlimit (48, 78, loopStartArea.getWidth() / 2);
        loopStartLabel.setBounds (loopStartArea.removeFromLeft (loopLabelWidth));
        loopStart.setBounds (loopStartArea.reduced (2, 2));
        loopEndLabel.setBounds (loopEndArea.removeFromLeft (juce::jlimit (44, 72, loopEndArea.getWidth() / 2)));
        loopEnd.setBounds (loopEndArea.reduced (2, 2));

        auto directRow = header.removeFromTop (32);
        const int targetLabelWidth = juce::jlimit (64, 95, directRow.getWidth() / 8);
        const int depthLabelWidth = juce::jlimit (46, 55, directRow.getWidth() / 12);
        targetLabel.setBounds (directRow.removeFromLeft (targetLabelWidth));
        const int controlsWidth = juce::jmax (1, directRow.getWidth() - depthLabelWidth - 8);
        const int targetWidth = juce::jlimit (110, 250, (int) std::round (controlsWidth * 0.46f));
        target.setBounds (directRow.removeFromLeft (juce::jmin (targetWidth, directRow.getWidth())).reduced (2, 2));
        if (directRow.getWidth() > depthLabelWidth + 8) directRow.removeFromLeft (8);
        depthLabel.setBounds (directRow.removeFromLeft (juce::jmin (depthLabelWidth, directRow.getWidth())));
        depth.setBounds (directRow.reduced (2, 2));'''
text = replace_once(text, old_header, new_header, "MSEG header layout")
old_routes = '''            routeLabels[(size_t) i].setBounds (row.removeFromLeft (70));
            routeSources[(size_t) i].setBounds (row.removeFromLeft (juce::jmax (140, row.getWidth() / 3)).reduced (3, 0));
            routeDestinations[(size_t) i].setBounds (row.removeFromLeft (juce::jmax (160, row.getWidth() / 2)).reduced (3, 0));
            routeAmounts[(size_t) i].setBounds (row.reduced (3, 0));'''
new_routes = '''            const int labelWidth = juce::jlimit (48, 70, row.getWidth() / 8);
            routeLabels[(size_t) i].setBounds (row.removeFromLeft (labelWidth));
            const int sourceWidth = juce::jmax (1, (int) std::round (row.getWidth() * 0.32f));
            routeSources[(size_t) i].setBounds (row.removeFromLeft (sourceWidth).reduced (3, 0));
            const int destinationWidth = juce::jmax (1, (int) std::round (row.getWidth() * 0.56f));
            routeDestinations[(size_t) i].setBounds (row.removeFromLeft (juce::jmin (destinationWidth, row.getWidth())).reduced (3, 0));
            routeAmounts[(size_t) i].setBounds (row.reduced (3, 0));'''
text = replace_once(text, old_routes, new_routes, "MSEG route layout")
write(path, text)

print("Responsive UI + Clang portability patch applied")
