#include "../Source/Analysis/MelodyAnalyzer.h"
#include "../Source/Engine/MelodyTransport.h"
#include "../Source/UI/AudioVisualBuffer.h"
#include <iostream>

bool runMelodyTests()
{
    auto fail = [] (const char* text) { std::cerr << "Melody test failed: " << text << '\n'; return false; };
    constexpr double sr = 22050.0;
    juce::AudioBuffer<float> audio (1, (int) (sr * 2.4)); audio.clear();
    const int pitches[] { 60, 64, 67, 72 };
    for (int n = 0; n < 4; ++n)
        for (int i = 0; i < (int) (sr * 0.45); ++i)
        {
            const double time = i / sr, phase = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (pitches[n]) * time;
            const float envelope = (float) std::min ({ 1.0, time / 0.01, (0.45 - time) / 0.025 });
            audio.setSample (0, (int) (sr * (0.1 + n * 0.55)) + i, envelope * (float) (0.5 * std::sin (phase) + 0.12 * std::sin (phase * 2)));
        }
    const auto clip = MelodyAnalyzer::analyzeBuffer (audio, sr, false, 120);
    std::cout << "Melody detected:"; for (const auto& n : clip.notes) std::cout << ' ' << n.pitch << '@' << n.start; std::cout << '\n';
    if (clip.notes.size() != 4) return fail ("incorrect melody note count");
    for (int i = 0; i < 4; ++i)
        if (clip.notes[(size_t) i].pitch != pitches[i] || std::abs (clip.notes[(size_t) i].start - (0.1 + i * 0.55)) > 0.10)
            return fail ("melody pitch/onset incorrect");
    if (! MelodyAnalyzer::analyzeBuffer (audio, sr, false, 120, [] { return true; }).notes.empty())
        return fail ("analysis cancellation ignored");
    audio.clear();
    if (! MelodyAnalyzer::analyzeBuffer (audio, sr, true, 120).notes.empty()) return fail ("silence produced notes");
    for (int i = (int) (sr * 0.2); i < (int) (sr * 0.9); ++i)
        for (const int note : { 60, 64, 67 })
            audio.addSample (0, i, (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) * i / sr)));
    const auto chord = MelodyAnalyzer::analyzeBuffer (audio, sr, true, 120);
    std::cout << "Chord detected:"; for (const auto& n : chord.notes) std::cout << ' ' << n.pitch; std::cout << '\n';
    for (const int expected : { 60, 64, 67 })
        if (std::none_of (chord.notes.begin(), chord.notes.end(), [expected] (const auto& n) { return n.pitch == expected && n.duration > 0.4; }))
            return fail ("layered mode missed a chord tone");
    if (chord.notes.size() > 4) return fail ("layered mode invented excessive notes");
    audio.clear();
    // Repeated pitches separated by short rests must become distinct MIDI notes.
    for (int repeat = 0; repeat < 3; ++repeat)
        for (int i = 0; i < (int) (sr * 0.2); ++i)
        {
            const double phase = juce::MathConstants<double>::twoPi * 329.6276 * i / sr;
            audio.setSample (0, (int) (sr * (0.1 + repeat * 0.3)) + i, (float) (0.4 * std::sin (phase)));
        }
    const auto repeated = MelodyAnalyzer::analyzeBuffer (audio, sr, false, 120);
    if (repeated.notes.size() != 3 || std::any_of (repeated.notes.begin(), repeated.notes.end(), [] (const auto& n) { return n.pitch != 64; }))
        return fail ("repeated notes were merged or mistranscribed");

    auto restored = MelodyClip::fromState (clip.toState());
    if (restored.notes.size() != clip.notes.size() || restored.notes[2].pitch != 67) return fail ("clip state round trip failed");
    restored.bpm = 173;
    juce::TemporaryFile midiFile (".mid");
    if (! restored.writeMidi (midiFile.getFile())) return fail ("MIDI file write failed");
    juce::FileInputStream input (midiFile.getFile()); juce::MidiFile midi;
    if (! midi.readFrom (input) || midi.getTimeFormat() != 960 || midi.getNumTracks() != 1) return fail ("invalid MIDI file");
    midi.convertTimestampTicksToSeconds();
    int ons = 0, offs = 0;
    for (int i = 0; i < midi.getTrack (0)->getNumEvents(); ++i)
    {
        const auto& message = midi.getTrack (0)->getEventPointer (i)->message;
        if (message.isNoteOn())
        {
            if (std::abs (message.getTimeStamp() - restored.notes[(size_t) ons].start) > 0.002) return fail ("MIDI tempo altered note timing");
            ++ons;
        }
        if (message.isNoteOff()) ++offs;
    }
    if (ons != 4 || offs != 4) return fail ("MIDI note pairs incomplete");

    MelodyTransport transport; MelodyClip probe;
    probe.notes.push_back ({ 60, 0.05, 0.15, 0.8f, 1.0f });
    transport.start (probe); juce::MidiBuffer events;
    transport.process (events, 100, 1000);
    if (events.getNumEvents() != 1 || (*events.begin()).samplePosition != 50 || ! (*events.begin()).getMessage().isNoteOn())
        return fail ("playback onset is not sample-accurate");
    events.clear(); transport.stop(); transport.process (events, 100, 1000);
    if (transport.isPlaying() || events.getNumEvents() != 1 || ! (*events.begin()).getMessage().isNoteOff())
        return fail ("stop left a stuck note");
    transport.start (probe); events.clear(); transport.process (events, 100, 1000);
    events.clear(); transport.process (events, 100, 1000); events.clear(); transport.process (events, 100, 1000);
    if (transport.isPlaying() || events.getNumEvents() != 1 || (*events.begin()).samplePosition != 0)
        return fail ("playback end did not release notes at the exact boundary");

    AudioVisualBuffer visual;
    juce::AudioBuffer<float> source (2, 256); source.clear(); source.setSample (0, 27, 0.25f); source.setSample (1, 27, -0.5f);
    visual.push (source); std::array<float, 256> left {}, right {};
    if (visual.read (left.data(), right.data(), 256) != 256 || left[27] != 0.25f || right[27] != -0.5f)
        return fail ("visualization FIFO changed output samples");
    return true;
}
