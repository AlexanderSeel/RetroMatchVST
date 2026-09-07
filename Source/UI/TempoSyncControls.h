#pragma once
#include "../PluginProcessor.h"
#include "../Engine/TempoSync.h"

class TempoSyncBar final : public juce::Component, private juce::Timer
{
public:
    explicit TempoSyncBar (RetroMatchSynthAudioProcessor& p) : proc (p)
    {
        source.addItemList ({ "MANUAL BPM", "DAW TEMPO" }, 1);
        bpm.setSliderStyle (juce::Slider::LinearHorizontal);
        bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
        bpm.setTextValueSuffix (" BPM");
        effective.setJustificationType (juce::Justification::centredLeft);
        effective.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        effective.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
        addAndMakeVisible (source); addAndMakeVisible (bpm); addAndMakeVisible (effective);
        sourceAttachment = std::make_unique<ComboAttachment> (proc.apvts, "tempoSource", source);
        bpmAttachment = std::make_unique<SliderAttachment> (proc.apvts, "manualBpm", bpm);
        startTimerHz (8);
        timerCallback();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        source.setBounds (r.removeFromLeft (118).reduced (2));
        bpm.setBounds (r.removeFromLeft (190).reduced (2));
        effective.setBounds (r.reduced (7, 2));
    }

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    RetroMatchSynthAudioProcessor& proc;
    juce::ComboBox source;
    juce::Slider bpm;
    juce::Label effective;
    std::unique_ptr<ComboAttachment> sourceAttachment;
    std::unique_ptr<SliderAttachment> bpmAttachment;

    void timerCallback() override
    {
        const bool manual = proc.apvts.getRawParameterValue ("tempoSource")->load() < 0.5f;
        bpm.setEnabled (manual);
        effective.setText ("CLOCK  " + juce::String (proc.getEffectiveBpm(), 1) + " BPM", juce::dontSendNotification);
    }
};

class TempoSyncSelector final : public juce::Component
{
public:
    TempoSyncSelector (RetroMatchSynthAudioProcessor& proc,
                       const juce::String& syncParameter,
                       const juce::String& divisionParameter,
                       const juce::String& caption = "SYNC")
    {
        sync.setButtonText (caption);
        division.addItemList (TempoSync::divisionLabels(), 1);
        addAndMakeVisible (sync); addAndMakeVisible (division);
        syncAttachment = std::make_unique<ButtonAttachment> (proc.apvts, syncParameter, sync);
        divisionAttachment = std::make_unique<ComboAttachment> (proc.apvts, divisionParameter, division);
        sync.onClick = [this] { division.setEnabled (sync.getToggleState()); };
        division.setEnabled (sync.getToggleState());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        sync.setBounds (r.removeFromLeft (70));
        division.setBounds (r.reduced (2));
    }

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    juce::ToggleButton sync;
    juce::ComboBox division;
    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<ComboAttachment> divisionAttachment;
};
