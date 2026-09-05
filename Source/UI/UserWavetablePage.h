#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include <array>
#include <memory>

class UserWavetablePage final : public juce::Component
{
public:
    explicit UserWavetablePage (RetroMatchSynthAudioProcessor& processor)
        : proc (processor)
    {
        setOpaque (true);

        title.setText ("USER WAVETABLE SET", juce::dontSendNotification);
        title.setColour (juce::Label::textColourId, juce::Colour (0xff65d5bc));
        title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));

        hint.setText ("Import single-cycle or multi-frame WAV/AIFF/FLAC/OGG tables. RetroMatch maps the source to five immutable 2048-sample frames while keeping the reference-derived wavetable separate.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, juce::Colour (0xff9db0aa));
        hint.setFont (juce::Font (juce::FontOptions (10.5f)));
        hint.setJustificationType (juce::Justification::topLeft);

        frameSizeLabel.setText ("SOURCE CYCLE", juce::dontSendNotification);
        frameSizeLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaebdb8));
        frameSizeLabel.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        frameSize.addItemList ({ "AUTO (prefer 2048)", "256", "512", "1024", "2048", "4096" }, 1);
        frameSize.setSelectedId (1, juce::dontSendNotification);
        frameSize.setTooltip ("Choose an explicit cycle size when a wavetable file length is divisible by multiple common frame sizes.");

        load.setButtonText ("LOAD WAVETABLE");
        clear.setButtonText ("CLEAR");
        load.onClick = [this] { chooseFile(); };
        clear.onClick = [this]
        {
            proc.clearUserWavetable();
            updateStatus();
            repaint();
        };

        mixLabel.setText ("USER WT MIX", juce::dontSendNotification);
        mixLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd1ad5d));
        mixLabel.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        mix.setSliderStyle (juce::Slider::LinearHorizontal);
        mix.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        mix.setNumDecimalPlacesToDisplay (2);
        mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "userWavetableMix", mix);

        status.setColour (juce::Label::textColourId, juce::Colour (0xffc2d2cc));
        status.setFont (juce::Font (juce::FontOptions (10.5f)));
        status.setJustificationType (juce::Justification::topLeft);

        addAndMakeVisible (title);
        addAndMakeVisible (hint);
        addAndMakeVisible (frameSizeLabel);
        addAndMakeVisible (frameSize);
        addAndMakeVisible (load);
        addAndMakeVisible (clear);
        addAndMakeVisible (mixLabel);
        addAndMakeVisible (mix);
        addAndMakeVisible (status);

        updateStatus();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b1012));
        auto outer = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff263034));
        g.drawRoundedRectangle (outer, 9.0f, 1.0f);

        auto preview = previewBounds.toFloat();
        g.setColour (juce::Colour (0xff11191b));
        g.fillRoundedRectangle (preview, 6.0f);
        g.setColour (juce::Colour (0xff314145));
        g.drawRoundedRectangle (preview, 6.0f, 1.0f);

        const auto table = proc.getUserWavetable();
        if (table == nullptr || ! table->valid)
        {
            g.setColour (juce::Colour (0xff70817b));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText ("NO USER WAVETABLE LOADED", previewBounds, juce::Justification::centred);
            return;
        }

        auto graph = preview.reduced (12.0f, 24.0f);
        const float frameHeight = graph.getHeight() / (float) ReferenceWavetableData::frameCount;
        for (int frame = 0; frame < ReferenceWavetableData::frameCount; ++frame)
        {
            auto row = juce::Rectangle<float> (graph.getX(), graph.getY() + frameHeight * frame,
                                                graph.getWidth(), frameHeight).reduced (0.0f, 5.0f);
            const float centreY = row.getCentreY();
            g.setColour (juce::Colour (0xff26383b));
            g.drawHorizontalLine ((int) centreY, row.getX(), row.getRight());

            juce::Path wave;
            constexpr int points = 180;
            for (int i = 0; i < points; ++i)
            {
                const double phase = i / (double) (points - 1);
                const float value = table->frames[(size_t) frame][(size_t) juce::jlimit (0, ReferenceWavetableData::tableSize - 1,
                    (int) std::floor (phase * (ReferenceWavetableData::tableSize - 1)))];
                const float x = juce::jmap ((float) i, 0.0f, (float) (points - 1), row.getX(), row.getRight());
                const float y = centreY - value * row.getHeight() * 0.43f;
                if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
            }

            g.setColour (juce::Colour (0xff65d5bc).withAlpha (0.16f));
            g.strokePath (wave, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
            g.setColour (frame == 2 ? juce::Colour (0xffd1ad5d) : juce::Colour (0xff65d5bc));
            g.strokePath (wave, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
            g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
            g.drawText ("F" + juce::String (frame + 1), (int) row.getX(), (int) row.getY(), 25, 14, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (28));
        hint.setBounds (area.removeFromTop (45));
        area.removeFromTop (6);

        auto controls = area.removeFromTop (38);
        frameSizeLabel.setBounds (controls.removeFromLeft (100));
        frameSize.setBounds (controls.removeFromLeft (160).reduced (3, 3));
        controls.removeFromLeft (10);
        load.setBounds (controls.removeFromLeft (145).reduced (3, 3));
        clear.setBounds (controls.removeFromLeft (80).reduced (3, 3));
        controls.removeFromLeft (12);
        mixLabel.setBounds (controls.removeFromLeft (85));
        mix.setBounds (controls.reduced (3, 3));

        status.setBounds (area.removeFromTop (42));
        area.removeFromTop (7);
        previewBounds = area;
    }

private:
    RetroMatchSynthAudioProcessor& proc;
    juce::Label title, hint, frameSizeLabel, mixLabel, status;
    juce::ComboBox frameSize;
    juce::TextButton load, clear;
    juce::Slider mix;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> previewBounds;

    int selectedFrameSize() const
    {
        switch (frameSize.getSelectedId())
        {
            case 2: return 256;
            case 3: return 512;
            case 4: return 1024;
            case 5: return 2048;
            case 6: return 4096;
            default: return 0;
        }
    }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load wavetable set", juce::File {}, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      proc.loadUserWavetable (file, selectedFrameSize());
                                  updateStatus();
                                  repaint();
                              });
    }

    void updateStatus()
    {
        if (! proc.hasUserWavetable())
        {
            status.setText ("No user table loaded. AUTO prefers standard 2048-sample cycles; choose a source cycle explicitly for ambiguous files.", juce::dontSendNotification);
            clear.setEnabled (false);
            return;
        }

        clear.setEnabled (true);
        status.setText (proc.getUserWavetableName() + "\n" + proc.getUserWavetableDescription(), juce::dontSendNotification);
    }
};
