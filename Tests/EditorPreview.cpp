#include "../Source/PluginProcessor.h"
#include "../Source/Engine/PresetLibrary.h"
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    if (argc < 2) return 2;
    const juce::File directory (juce::String::fromUTF8 (argv[1]));
    if (directory.createDirectory().failed()) return 3;
    auto processor = std::make_unique<RetroMatchSynthAudioProcessor>();
    processor->setPlayConfigDetails (0, 2, 44100, 256); processor->prepareToPlay (44100, 256);
    MelodyClip melody; melody.sourceName = "Synthetic UI test melody"; melody.duration = 2.4;
    const int pitches[] { 60, 64, 67, 72 };
    for (int i = 0; i < 4; ++i) melody.notes.push_back ({ pitches[i], 0.1 + i * 0.55, 0.42, 0.8f, 0.95f });
    const auto fixture = directory.getChildFile ("melody-fixture.wav");
    {
        constexpr int rate = 22050, samples = (int) (rate * 2.4);
        juce::FileOutputStream stream (fixture);
        if (! stream.openedOk()) return 14;
        stream.setPosition (0); stream.truncate();
        stream.write ("RIFF", 4); stream.writeInt (36 + samples * 2); stream.write ("WAVEfmt ", 8);
        stream.writeInt (16); stream.writeShort (1); stream.writeShort (1); stream.writeInt (rate);
        stream.writeInt (rate * 2); stream.writeShort (2); stream.writeShort (16);
        stream.write ("data", 4); stream.writeInt (samples * 2);
        for (int i = 0; i < samples; ++i)
        {
            const double time = (double) i / rate; double value = 0.0;
            for (const auto& note : melody.notes)
            {
                const double t = time - note.start;
                if (t >= 0.0 && t < note.duration)
                {
                    const double phase = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note.pitch) * t;
                    const double envelope = std::min ({ 1.0, t / 0.01, (note.duration - t) / 0.02 });
                    value += (std::sin (phase) * 0.5 + std::sin (phase * 2) * 0.15) * envelope;
                }
            }
            stream.writeShort ((short) (value * 28000.0));
        }
    }
    if (! processor->loadReferenceSample (fixture)) return 15;
    const auto extracted = MelodyAnalyzer::analyzeFile (fixture, false, 120);
    if (extracted.notes.size() != 4) return 16;
    processor->setMelodyClip (extracted);
    if (! processor->setReferenceAnalysisRegion (0.1f, 0.5f)
        || ! processor->createUserWavetableFromReference (0.1f, 0.5f)) return 17;
    processor->apvts.getParameter ("distortionMode")->setValueNotifyingHost (0.5f);
    auto set = [&] (const char* id, float value) { auto* parameter = processor->apvts.getParameter (id); parameter->setValueNotifyingHost (parameter->convertTo0to1 (value)); };
    set ("fxModule1Type", 3); set ("fxModule2Type", 8); set ("fxModule2Stage", 1);
    set ("moduleMod1Source", 7); set ("moduleMod1Dest", 7); set ("moduleMod1Amount", 0.5f);
    processor->captureLayer (0);
    processor->captureLayer (1);
    if (! processor->hasLayer (0) || ! processor->hasLayer (1)) return 18;
    processor->clearUserWavetable();
    processor->lightPalette.store (3);
    juce::MemoryBlock state; processor->getStateInformation (state);
    auto restored = std::make_unique<RetroMatchSynthAudioProcessor>();
    restored->setStateInformation (state.getData(), (int) state.getSize());
    if (restored->lightPalette.load() != 3 || restored->getMelodyClip().notes.size() != 4) return 4;
    if (! restored->hasLayer (0) || ! restored->hasLayer (1) || ! restored->loadLayerToMain (0)
        || ! restored->hasUserWavetable() || restored->getCurrentVoiceParameters().distortionMode != 1) return 19;
    if (restored->getCurrentVoiceParameters().fxModules[1].type != 8
        || restored->getCurrentVoiceParameters().moduleModSlots[0].source != 7) return 29;
    const auto preset = directory.getChildFile ("layers-test.xml");
    if (! processor->savePreset (preset)) return 20;
    restored->clearLayer (0); restored->clearLayer (1);
    if (! restored->loadPreset (preset) || ! restored->hasLayer (0) || ! restored->hasLayer (1)) return 21;
    restored.reset(); processor->lightPalette.store (0);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    editor->setVisible (true); editor->setSize (1520, 920);
    juce::TabbedComponent* tabs = nullptr;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
        if (auto* found = dynamic_cast<juce::TabbedComponent*> (editor->getChildComponent (i))) tabs = found;
    if (! tabs) return 5;
    juce::TextButton* applyRegion = nullptr;
    juce::TextButton* createTable = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            if (slider->getComponentID() == "referenceStart") slider->setValue (0.15);
            if (slider->getComponentID() == "referenceEnd") slider->setValue (0.4);
        }
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getButtonText() == "APPLY") applyRegion = button;
            if (button->getButtonText() == "CREATE WAVETABLE") createTable = button;
        }
    }
    if (! applyRegion || ! createTable) return 27;
    applyRegion->onClick(); createTable->onClick();
    if (std::abs (processor->getAnalysisStartSeconds() - 0.15f) > 0.001f
        || std::abs (processor->getAnalysisEndSeconds() - 0.4f) > 0.001f || ! processor->hasUserWavetable()) return 28;
    auto capture = [&] (const juce::String& name)
    {
        if (name.startsWith ("02"))
            for (int block = 0; block < 20; ++block)
            {
                juce::AudioBuffer<float> output (2, 256); juce::MidiBuffer events;
                processor->processBlock (output, events);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
            }
        else juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        const auto file = directory.getChildFile (name + ".png");
        auto output = file.createOutputStream();
        if (! output) return false;
        output->setPosition (0); output->truncate();
        return juce::PNGImageFormat().writeImageToStream (image, *output);
    };
    if (! capture ("01-synth-mint")) return 6;
    tabs->setCurrentTabIndex (7);
    for (int block = 0; block < 40; ++block)
    {
        juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi;
        if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        processor->processBlock (audio, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
    }
    if (! capture ("02-signal-mint")) return 7;
    tabs->setCurrentTabIndex (8);
    bool foundLightSwitch = false;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*> (editor->getChildComponent (i)))
            if (button->getButtonText().startsWith ("LED:")) { button->triggerClick(); foundLightSwitch = true; break; }
    if (! foundLightSwitch) return 12;
    if (! capture ("03-melody-amber")) return 8;
    if (processor->lightPalette.load() != 1) return 13;
    auto* melodyPage = tabs->getTabContentComponent (8);
    juce::TextButton* melodyPlay = nullptr;
    for (auto* child : melodyPage->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            if (button->getButtonText() == "PLAY MELODY") melodyPlay = button;
    if (! melodyPlay) return 22;
    // Exercise the actual button: main synth muted, so only captured layers can sound.
    processor->apvts.getParameter ("mainLayerGain")->setValueNotifyingHost (0.0f);
    melodyPlay->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi;
    processor->processBlock (audio, midi);
    if (! processor->melodyTransport.isPlaying()) return 9;
    float melodyPeak = 0;
    for (int block = 0; block < 80; ++block)
    {
        midi.clear(); processor->processBlock (audio, midi);
        melodyPeak = std::max (melodyPeak, audio.getMagnitude (0, audio.getNumSamples()));
    }
    if (melodyPeak < 0.001f || processor->getReferenceAuditionMode() != RetroMatchSynthAudioProcessor::ReferenceAuditionMode::synthOnly) return 23;
    editor.reset(); midi.clear(); processor->processBlock (audio, midi);
    if (processor->melodyTransport.isPlaying()) return 10;
    editor.reset (processor->createEditor()); editor->setVisible (true);
    editor->setSize (1180, 760); processor->lightPalette.store (3);
    if (! capture ("04-minimum-violet")) return 11;
    tabs = nullptr;
    for (auto* child : editor->getChildren()) if (auto* found = dynamic_cast<juce::TabbedComponent*> (child)) tabs = found;
    if (! tabs) return 24;
    tabs->setCurrentTabIndex (4); if (! capture ("05-distortion-minimum")) return 25;
    tabs->setCurrentTabIndex (tabs->getTabNames().indexOf ("LAYERS")); if (! capture ("06-layers-minimum")) return 26;
    tabs->setCurrentTabIndex (3); if (! capture ("07-modulators-minimum")) return 30;
    tabs->setCurrentTabIndex (tabs->getTabNames().indexOf ("WAVETABLE")); if (! capture ("08-wavetable-minimum")) return 31;
    tabs->setCurrentTabIndex (tabs->getTabNames().indexOf ("PRESETS")); if (! capture ("09-presets-minimum")) return 32;
    auto* presetPage = tabs->getCurrentContentComponent();
    juce::TextButton* randomize = nullptr;
    for (auto* child : presetPage->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child)) if (button->getButtonText() == "RANDOMIZE NEW PATCH") randomize = button;
    if (! randomize) return 33;
    randomize->onClick();
    if (! processor->getPresetName().startsWith ("Random / ")) return 34;
    editor.reset();
    auto factoryProcessor = std::make_unique<RetroMatchSynthAudioProcessor>();
    const auto factoryDirectory = directory.getChildFile ("Factory Presets");
    if (factoryDirectory.createDirectory().failed()) return 35;
    for (int i = 0; i < (int) factoryPresetCatalog.size(); ++i)
    {
        factoryProcessor->loadFactoryPreset (i);
        auto file = factoryDirectory.getChildFile (juce::String (factoryPresetCatalog[(size_t) i].name) + ".xml");
        if (! factoryProcessor->savePreset (file) || ! factoryProcessor->loadPreset (file)) return 36;
        if (factoryProcessor->getPresetName() != factoryPresetCatalog[(size_t) i].name) return 37;
        if (i == 5 && ! factoryProcessor->hasLayer (0)) return 38;
    }
    // Loading a pre-module patch must reset new settings rather than retain the previous rack.
    auto legacy = factoryProcessor->apvts.copyState();
    for (int i = legacy.getNumChildren(); --i >= 0;)
    {
        auto child = legacy.getChild (i); const auto id = child["id"].toString();
        if (child.hasType ("SYNTH_LAYERS") || id.startsWith ("layer") || id.startsWith ("fxModule")
            || id.startsWith ("moduleMod") || id.startsWith ("lfoModule") || id == "mainLayerGain") legacy.removeChild (i, nullptr);
    }
    const auto legacyFile = directory.getChildFile ("legacy-test.xml");
    if (! legacy.createXml()->writeTo (legacyFile, {}) || ! factoryProcessor->loadPreset (legacyFile)) return 39;
    if (factoryProcessor->hasLayer (0) || factoryProcessor->getCurrentVoiceParameters().fxModules[0].type != 0) return 40;
    std::cout << "Editor snapshots and session/playback lifecycle checks passed.\n";
    return 0;
}
