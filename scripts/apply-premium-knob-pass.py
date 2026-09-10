from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)

# FX rack: musical module parameters use rotary hardware controls; structure stays in selectors/buttons.
fx_path = Path("Source/UI/FxRackPage.h")
fx = fx_path.read_text(encoding="utf-8")
fx = replace_once(
    fx,
    '                slider.setSliderStyle (juce::Slider::LinearVertical); slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 18);\n',
    '                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);\n'
    '                slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 18);\n'
    '                slider.setMouseDragSensitivity (180);\n'
    '                slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,\n'
    '                                            juce::MathConstants<float>::pi * 2.8f, true);\n',
    "FX slider style",
)
fx = replace_once(
    fx,
    '''            row.panel.setBounds (0, y, width, 196); y += 206;\n            auto r = row.panel.getLocalBounds(); auto header = r.removeFromTop (30);\n            row.type.setBounds (header.removeFromLeft (juce::jmax (100, width - 320)).reduced (2)); row.stage.setBounds (header.removeFromLeft (70).reduced (2));\n            row.bypass.setBounds (header.removeFromLeft (78)); row.up.setBounds (header.removeFromLeft (36).reduced (2)); row.down.setBounds (header.removeFromLeft (36).reduced (2));\n            row.copy.setBounds (header.removeFromLeft (53).reduced (2)); row.remove.setBounds (header.reduced (2));\n            if (row.sync && row.sync->isVisible()) row.sync->setBounds (r.removeFromTop (30).removeFromRight (220).reduced (2));\n            else r.removeFromTop (30);\n            row.visual.setBounds (r.removeFromLeft (width * 2 / 5).reduced (3)); const int w = r.getWidth() / 4;\n            for (int k = 0; k < 4; ++k) { auto control = r.removeFromLeft (w).reduced (2); row.labels[(size_t) k].setBounds (control.removeFromTop (24)); row.controls[(size_t) k].setBounds (control); }\n''',
    '''            constexpr int rowHeight = 224;\n            constexpr int rowGap = 10;\n            row.panel.setBounds (0, y, width, rowHeight); y += rowHeight + rowGap;\n\n            auto r = row.panel.getLocalBounds();\n            auto header = r.removeFromTop (34);\n            const int fixedHeaderWidth = 72 + 78 + 38 + 38 + 56 + 38;\n            row.type.setBounds (header.removeFromLeft (juce::jmax (150, width - fixedHeaderWidth)).reduced (2));\n            row.stage.setBounds (header.removeFromLeft (72).reduced (2));\n            row.bypass.setBounds (header.removeFromLeft (78).reduced (2));\n            row.up.setBounds (header.removeFromLeft (38).reduced (2));\n            row.down.setBounds (header.removeFromLeft (38).reduced (2));\n            row.copy.setBounds (header.removeFromLeft (56).reduced (2));\n            row.remove.setBounds (header.reduced (2));\n\n            auto syncBand = r.removeFromTop (32);\n            if (row.sync && row.sync->isVisible()) row.sync->setBounds (syncBand.removeFromRight (236).reduced (3, 2));\n\n            auto body = r.reduced (2, 3);\n            const int previewWidth = juce::jlimit (260, 390, body.getWidth() * 2 / 5);\n            row.visual.setBounds (body.removeFromLeft (previewWidth).reduced (3));\n            body.removeFromLeft (8);\n\n            const int cellWidth = juce::jmax (1, body.getWidth() / 4);\n            for (int k = 0; k < 4; ++k)\n            {\n                auto cell = body.removeFromLeft (k == 3 ? body.getWidth() : cellWidth).reduced (4, 0);\n                row.labels[(size_t) k].setBounds (cell.removeFromTop (22));\n                // Give the rotary control a real hardware-sized hit target. The LookAndFeel\n                // uses the smaller dimension for a circular knob and leaves the value box below.\n                row.controls[(size_t) k].setBounds (cell.reduced (2, 0));\n            }\n''',
    "FX row layout",
)
fx_path.write_text(fx, encoding="utf-8")

# Independent modulators: LFO rate becomes a normal synth knob; route depth becomes a compact bipolar trim knob.
mod_path = Path("Source/UI/ModulatorsPage.h")
mod = mod_path.read_text(encoding="utf-8")
mod = replace_once(
    mod,
    '''            auto& rate = rates[(size_t) i]; auto& shape = shapes[(size_t) i];\n            addAndMakeVisible (rate); addAndMakeVisible (shape);\n            rate.setSliderStyle (juce::Slider::LinearHorizontal); rate.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20); rate.setTextValueSuffix (" Hz");\n''',
    '''            auto& rate = rates[(size_t) i]; auto& shape = shapes[(size_t) i];\n            addAndMakeVisible (rate); addAndMakeVisible (shape); addAndMakeVisible (rateLabels[(size_t) i]);\n            rate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);\n            rate.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);\n            rate.setTextValueSuffix (" Hz");\n            rate.setMouseDragSensitivity (180);\n            rate.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,\n                                      juce::MathConstants<float>::pi * 2.8f, true);\n            rateLabels[(size_t) i].setText ("RATE", juce::dontSendNotification);\n            rateLabels[(size_t) i].setJustificationType (juce::Justification::centred);\n            rateLabels[(size_t) i].setColour (juce::Label::textColourId, juce::Colour (0xffb8c8c3));\n            rateLabels[(size_t) i].setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));\n''',
    "LFO rate setup",
)
mod = replace_once(
    mod,
    '            amount.setSliderStyle (juce::Slider::LinearHorizontal); amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 24);\n',
    '''            amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);\n            amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);\n            amount.setMouseDragSensitivity (160);\n            amount.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,\n                                        juce::MathConstants<float>::pi * 2.8f, true);\n''',
    "mod depth setup",
)
mod = replace_once(
    mod,
    '''        auto r = getLocalBounds().reduced (16); r.removeFromTop (40); auto lfos = r.removeFromTop (250); const int w = lfos.getWidth() / 3;\n        for (int i = 0; i < 3; ++i)\n        {\n            auto column = lfos.removeFromLeft (w).reduced (5); plots[(size_t) i] = column.removeFromTop (125);\n            shapes[(size_t) i].setBounds (column.removeFromTop (27).reduced (2));\n            rates[(size_t) i].setBounds (column.removeFromTop (48).reduced (2));\n            syncs[(size_t) i]->setBounds (column.removeFromTop (30).reduced (2));\n        }\n        r.removeFromTop (26);\n        for (int i = 0; i < 4; ++i)\n        {\n            auto row = r.removeFromTop (42); sources[(size_t) i].setBounds (row.removeFromLeft (r.getWidth() / 3).reduced (3));\n            destinations[(size_t) i].setBounds (row.removeFromLeft (r.getWidth() / 3).reduced (3)); amounts[(size_t) i].setBounds (row.reduced (3));\n        }\n''',
    '''        auto r = getLocalBounds().reduced (12);\n        r.removeFromTop (36);\n        const int lfoHeight = juce::jlimit (210, 230, r.getHeight() - 188);\n        auto lfos = r.removeFromTop (lfoHeight);\n        const int columnWidth = lfos.getWidth() / 3;\n        for (int i = 0; i < 3; ++i)\n        {\n            auto column = lfos.removeFromLeft (i == 2 ? lfos.getWidth() : columnWidth).reduced (6, 3);\n            const int plotHeight = juce::jmax (78, column.getHeight() - 130);\n            plots[(size_t) i] = column.removeFromTop (plotHeight);\n            column.removeFromTop (4);\n            shapes[(size_t) i].setBounds (column.removeFromTop (28).reduced (2));\n            rateLabels[(size_t) i].setBounds (column.removeFromTop (16));\n            auto knobBand = column.removeFromTop (juce::jmax (54, column.getHeight() - 28));\n            const int knobWidth = juce::jmin (96, knobBand.getWidth());\n            rates[(size_t) i].setBounds (knobBand.withSizeKeepingCentre (knobWidth, knobBand.getHeight()));\n            syncs[(size_t) i]->setBounds (column.removeFromTop (28).reduced (2));\n        }\n\n        r.removeFromTop (8);\n        routesHeaderBounds = r.removeFromTop (22);\n        const int routeHeight = juce::jmax (36, r.getHeight() / 4);\n        for (int i = 0; i < 4; ++i)\n        {\n            auto row = r.removeFromTop (i == 3 ? r.getHeight() : routeHeight);\n            const int third = juce::jmax (1, row.getWidth() / 3);\n            sources[(size_t) i].setBounds (row.removeFromLeft (third).reduced (3, 4));\n            destinations[(size_t) i].setBounds (row.removeFromLeft (third).reduced (3, 4));\n            auto depth = row.reduced (3, 1);\n            const int trimWidth = juce::jmin (142, depth.getWidth());\n            amounts[(size_t) i].setBounds (depth.withWidth (trimWidth));\n        }\n''',
    "mod page layout",
)
mod = replace_once(
    mod,
    '        g.setColour (led); g.setFont (12); g.drawText ("SOURCE                                DESTINATION                                  DEPTH", 20, 306, getWidth() - 40, 26, juce::Justification::centredLeft);\n',
    '''        g.setColour (led);\n        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));\n        auto header = routesHeaderBounds.toFloat();\n        const float third = header.getWidth() / 3.0f;\n        g.drawText ("SOURCE", header.withWidth (third).reduced (4.0f, 0.0f), juce::Justification::centredLeft);\n        g.drawText ("DESTINATION", header.withTrimmedLeft (third).withWidth (third).reduced (4.0f, 0.0f), juce::Justification::centredLeft);\n        g.drawText ("DEPTH", header.withTrimmedLeft (third * 2.0f).reduced (4.0f, 0.0f), juce::Justification::centredLeft);\n''',
    "route headings",
)
mod = replace_once(
    mod,
    '    std::array<juce::Rectangle<int>, 3> plots;\n    std::array<juce::Slider, 3> rates; std::array<juce::ComboBox, 3> shapes;\n',
    '''    std::array<juce::Rectangle<int>, 3> plots;\n    juce::Rectangle<int> routesHeaderBounds;\n    std::array<juce::Slider, 3> rates; std::array<juce::ComboBox, 3> shapes;\n    std::array<juce::Label, 3> rateLabels;\n''',
    "mod members",
)
mod_path.write_text(mod, encoding="utf-8")

print("Applied premium FX/LFO/mod-depth rotary control pass")
