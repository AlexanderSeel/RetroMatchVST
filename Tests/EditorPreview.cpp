#include "../Source/PluginProcessor.h"
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
    processor->lightPalette.store (3);
    juce::MemoryBlock state; processor->getStateInformation (state);
    auto restored = std::make_unique<RetroMatchSynthAudioProcessor>();
    restored->setStateInformation (state.getData(), (int) state.getSize());
    if (restored->lightPalette.load() != 3 || restored->getMelodyClip().notes.size() != 4) return 4;
    restored.reset(); processor->lightPalette.store (0);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    editor->setVisible (true); editor->setSize (1520, 920);
    juce::TabbedComponent* tabs = nullptr;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
        if (auto* found = dynamic_cast<juce::TabbedComponent*> (editor->getChildComponent (i))) tabs = found;
    if (! tabs) return 5;
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
    processor->playMelody();
    juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi;
    processor->processBlock (audio, midi);
    if (! processor->melodyTransport.isPlaying()) return 9;
    editor.reset(); midi.clear(); processor->processBlock (audio, midi);
    if (processor->melodyTransport.isPlaying()) return 10;
    editor.reset (processor->createEditor()); editor->setVisible (true);
    editor->setSize (1180, 760); processor->lightPalette.store (3);
    if (! capture ("04-minimum-violet")) return 11;
    std::cout << "Editor snapshots and session/playback lifecycle checks passed.\n";
    return 0;
}
