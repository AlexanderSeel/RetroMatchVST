from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)

header_path = Path("Source/UI/RetroMatchEditorV3.h")
cpp_path = Path("Source/UI/RetroMatchEditorV3.cpp")
header = header_path.read_text(encoding="utf-8")
cpp = cpp_path.read_text(encoding="utf-8")

stereo_class = '''    class StereoMeter final : public juce::Component
    {
    public:
        explicit StereoMeter (RetroMatchSynthAudioProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    private:
        RetroMatchSynthAudioProcessor& proc;
    };
'''
compact_class = stereo_class + '''
    class CompactMasterMeter final : public juce::Component
    {
    public:
        explicit CompactMasterMeter (RetroMatchSynthAudioProcessor& p) : proc (p)
        {
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            if (bounds.getWidth() < 30.0f || bounds.getHeight() < 22.0f) return;

            const auto mint = findColour (RetroLookAndFeel::primaryLed);
            const auto amber = findColour (RetroLookAndFeel::secondaryLed);
            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), 4.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff11191b), bounds.getTopLeft(),
                                                     juce::Colour (0xff070c0e), bounds.getBottomLeft(), false));
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (juce::Colour (0xff4a5b5e));
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

            auto content = bounds.reduced (5.0f, 4.0f);
            const std::array<float, 2> peaks { proc.getOutputPeakLeft(), proc.getOutputPeakRight() };
            constexpr int segments = 12;
            const float labelW = 10.0f;
            const float dbW = 27.0f;
            const float rowGap = 3.0f;
            const float rowH = (content.getHeight() - rowGap) * 0.5f;

            for (int channel = 0; channel < 2; ++channel)
            {
                auto row = juce::Rectangle<float> (content.getX(), content.getY() + channel * (rowH + rowGap),
                                                   content.getWidth(), rowH);
                auto label = row.removeFromLeft (labelW);
                auto dbArea = row.removeFromRight (dbW);
                auto bar = row.reduced (2.0f, juce::jmax (0.0f, (row.getHeight() - 7.0f) * 0.5f));
                const float db = juce::jlimit (-60.0f, 3.0f,
                                               20.0f * std::log10 (juce::jmax (0.0001f, peaks[(size_t) channel])));
                const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -48.0f, 0.0f, 0.0f, 1.0f));
                const int lit = (int) std::ceil (norm * segments);
                const float gap = 1.25f;
                const float segmentW = (bar.getWidth() - gap * (segments - 1)) / segments;

                g.setColour (juce::Colour (0xffaab9b5));
                g.setFont (juce::Font (juce::FontOptions (7.5f, juce::Font::bold)));
                g.drawText (channel == 0 ? "L" : "R", label, juce::Justification::centredLeft);

                for (int i = 0; i < segments; ++i)
                {
                    auto seg = juce::Rectangle<float> (bar.getX() + i * (segmentW + gap), bar.getY(), segmentW, bar.getHeight());
                    auto colour = i >= 11 ? juce::Colour (0xffff6b5f)
                                          : (i >= 9 ? amber : mint);
                    if (i >= lit) colour = juce::Colour (0xff253134);
                    else
                    {
                        g.setColour (colour.withAlpha (0.14f));
                        g.fillRoundedRectangle (seg.expanded (1.0f), 1.0f);
                    }
                    g.setColour (colour);
                    g.fillRoundedRectangle (seg, 0.8f);
                }

                g.setColour (db > -1.0f ? juce::Colour (0xffff8b73) : juce::Colour (0xffb9c8c3));
                g.setFont (juce::Font (juce::FontOptions (7.2f, juce::Font::bold)));
                g.drawText (db <= -59.5f ? "-∞" : juce::String (db, 0), dbArea, juce::Justification::centredRight);
            }
        }

    private:
        RetroMatchSynthAudioProcessor& proc;
    };
'''
header = replace_once(header, stereo_class, compact_class, "insert CompactMasterMeter")
header = replace_once(header, "    StereoMeter outputMeter;\n", "    StereoMeter outputMeter;\n    CompactMasterMeter masterMeter;\n", "master meter member")

cpp = replace_once(
    cpp,
    ": AudioProcessorEditor (&p), proc (p), filterGraph (p), envelopeGraph (p), lfoScope (p), outputMeter (p), aiSettings (AISettings::load())",
    ": AudioProcessorEditor (&p), proc (p), filterGraph (p), envelopeGraph (p), lfoScope (p), outputMeter (p), masterMeter (p), aiSettings (AISettings::load())",
    "master meter initializer")
cpp = replace_once(cpp, '    masterOutputLabel.setText ("MASTER", juce::dontSendNotification);\n',
                   '    masterOutputLabel.setText ("MASTER OUT", juce::dontSendNotification);\n', "master label")
cpp = replace_once(cpp, '    masterOutput.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);\n',
                   '    masterOutput.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);\n', "master textbox")
cpp = replace_once(cpp,
                   '    addAndMakeVisible (masterOutputLabel); addAndMakeVisible (masterOutput);\n',
                   '    addAndMakeVisible (masterOutputLabel); addAndMakeVisible (masterOutput); addAndMakeVisible (masterMeter);\n',
                   "show master meter")

old_header_layout = '''    auto header = outer.removeFromTop (58);
    logoBounds = header.removeFromLeft (62).reduced (3);
    title.setBounds (header.removeFromLeft (230));
    subtitle.setBounds (header.removeFromLeft (juce::jmax (120, header.getWidth() - 490)));
    auto masterArea = header.removeFromLeft (100);
    masterOutputLabel.setBounds (masterArea.removeFromTop (15));
    masterOutput.setBounds (masterArea.reduced (2, 1));
    const int actionW = juce::jmax (72, header.getWidth() / 5);
    lightSwitch.setBounds (header.removeFromRight (actionW).reduced (3, 9));
    keyboardToggle.setBounds (header.removeFromRight (actionW).reduced (3, 9));
    exportPreview.setBounds (header.removeFromRight (actionW).reduced (3, 9));
    savePatch.setBounds (header.removeFromRight (actionW).reduced (3, 9));
    loadPatch.setBounds (header.reduced (3, 9));
    outer.removeFromTop (10);
'''
new_header_layout = '''    auto header = outer.removeFromTop (72);
    logoBounds = header.removeFromLeft (62).reduced (3, 8);
    title.setBounds (header.removeFromLeft (230));

    // Keep file/session actions grouped on the far right and mount MASTER OUT
    // as a dedicated channel strip immediately before them. This avoids the
    // old tiny rotary floating between the product subtitle and the buttons.
    const int actionW = juce::jlimit (78, 102, getWidth() / 15);
    lightSwitch.setBounds (header.removeFromRight (actionW).reduced (3, 15));
    keyboardToggle.setBounds (header.removeFromRight (actionW).reduced (3, 15));
    exportPreview.setBounds (header.removeFromRight (actionW).reduced (3, 15));
    savePatch.setBounds (header.removeFromRight (actionW).reduced (3, 15));
    loadPatch.setBounds (header.removeFromRight (actionW).reduced (3, 15));
    header.removeFromRight (6);

    auto masterArea = header.removeFromRight (172).reduced (3, 5);
    masterOutputLabel.setBounds (masterArea.removeFromTop (14));
    auto masterBody = masterArea.reduced (1, 0);
    masterOutput.setBounds (masterBody.removeFromLeft (76).reduced (2, 0));
    masterBody.removeFromLeft (4);
    masterMeter.setBounds (masterBody.reduced (1, 3));
    header.removeFromRight (8);
    subtitle.setBounds (header.reduced (8, 0));
    outer.removeFromTop (10);
'''
cpp = replace_once(cpp, old_header_layout, new_header_layout, "header layout")
cpp = replace_once(cpp,
                   '                                               (float) getWidth() - ((float) cheek + 9.0f) * 2.0f, 64.0f);\n',
                   '                                               (float) getWidth() - ((float) cheek + 9.0f) * 2.0f, 78.0f);\n',
                   "header frame height")
cpp = replace_once(cpp, '    outputMeter.repaint();\n', '    outputMeter.repaint();\n    masterMeter.repaint();\n', "master meter repaint")

header_path.write_text(header, encoding="utf-8")
cpp_path.write_text(cpp, encoding="utf-8")
print("Applied dedicated MASTER OUT strip with compact stereo peak meter.")
