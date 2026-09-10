#pragma once
#include "../Analysis/MelodyAnalyzer.h"
#include "../Sequencer/StepSequencer.h"
#include <atomic>
#include <vector>

// Message-thread commands, sample-accurate audio-thread scheduling. The audio
// thread never waits for the UI and owns the live transport/event storage.
// Event storage is allocated once on construction: a long transcription can
// contain thousands of notes, so fixed std::array storage here would make every
// MelodyTransport instance enormous and can overflow the Windows thread stack.
class MelodyTransport
{
public:
    MelodyTransport() : pending ((size_t) capacity), events ((size_t) capacity)
    {
        sequencerInput.ensureSize (131072);
    }

    void start (const MelodyClip& clip)
    {
        const juce::SpinLock::ScopedLockType guard (commandLock);
        pendingCount = 0;
        for (const auto& note : clip.notes)
        {
            if (pendingCount + 2 > capacity) break;
            pending[(size_t) pendingCount++] = { note.start, note.pitch, (int) std::lround (note.velocity * 127.0f) };
            pending[(size_t) pendingCount++] = { note.start + note.duration, note.pitch, 0 };
        }
        std::sort (pending.begin(), pending.begin() + pendingCount, [] (const auto& a, const auto& b)
        { return a.seconds == b.seconds ? a.velocity < b.velocity : a.seconds < b.seconds; });
        pendingStart = pendingCount > 0;
        commandReady.store (true, std::memory_order_release);
    }
    void stop()
    {
        const juce::SpinLock::ScopedLockType guard (commandLock);
        pendingStart = false; pendingCount = 0;
        commandReady.store (true, std::memory_order_release);
    }
    bool isPlaying() const { return playing.load(); }
    double getPosition() const { return position.load(); }

    void setSequencerSettings (RetroMatchSequencer::Settings settings)
    {
        const juce::SpinLock::ScopedLockType guard (sequencerCommandLock);
        pendingSequencerSettings = settings;
        sequencerCommandReady.store (true, std::memory_order_release);
    }
    RetroMatchSequencer::Settings getSequencerSettings() const
    {
        const juce::SpinLock::ScopedLockType guard (sequencerCommandLock);
        return pendingSequencerSettings;
    }
    void setSequencerStep (int index, RetroMatchSequencer::Step step)
    {
        if (! juce::isPositiveAndBelow (index, RetroMatchSequencer::maxSteps)) return;
        const juce::SpinLock::ScopedLockType guard (sequencerCommandLock);
        pendingSequencerSteps[(size_t) index] = step;
        pendingSequencerPatternDirty = true;
        sequencerCommandReady.store (true, std::memory_order_release);
    }
    RetroMatchSequencer::Step getSequencerStep (int index) const
    {
        index = juce::jlimit (0, RetroMatchSequencer::maxSteps - 1, index);
        const juce::SpinLock::ScopedLockType guard (sequencerCommandLock);
        return pendingSequencerSteps[(size_t) index];
    }
    void clearSequencerPattern()
    {
        const juce::SpinLock::ScopedLockType guard (sequencerCommandLock);
        for (auto& step : pendingSequencerSteps)
        {
            step = RetroMatchSequencer::Step {};
            step.rest = true;
        }
        pendingSequencerPatternDirty = true;
        sequencerCommandReady.store (true, std::memory_order_release);
    }
    void randomizeSequencerPattern (std::uint32_t seed)
    {
        RetroMatchSequencer::Core generator;
        generator.setSettings (getSequencerSettings());
        generator.randomizePattern (seed, -12, 12, 0.18f);
        const juce::SpinLock::ScopedLockType guard (sequencerCommandLock);
        for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i)
            pendingSequencerSteps[(size_t) i] = generator.getStep (i);
        pendingSequencerPatternDirty = true;
        sequencerCommandReady.store (true, std::memory_order_release);
    }
    int getSequencerCurrentStep() const noexcept { return sequencerCurrentStep.load (std::memory_order_relaxed); }
    bool isSequencerRunning() const noexcept { return sequencerRunning.load (std::memory_order_relaxed); }

    void process (juce::MidiBuffer& midi, int samples, double sr)
    {
        if (samples <= 0 || sr <= 0.0) return;
        applySequencerCommands (midi, sr);
        processSequencer (midi, samples);

        if (commandReady.load (std::memory_order_acquire))
        {
            const juce::SpinLock::ScopedTryLockType guard (commandLock);
            if (guard.isLocked())
            {
                releaseNotes (midi, 0);
                count = pendingCount; std::copy_n (pending.begin(), count, events.begin());
                next = 0; elapsed = 0.0; position.store (0.0); playing.store (pendingStart);
                commandReady.store (false, std::memory_order_release);
            }
        }
        if (! playing.load()) return;
        const double end = elapsed + samples / sr;
        while (next < count && events[(size_t) next].seconds < end)
        {
            const auto& e = events[(size_t) next++];
            const int offset = juce::jlimit (0, samples - 1, (int) std::lround ((e.seconds - elapsed) * sr));
            midi.addEvent (e.velocity > 0 ? juce::MidiMessage::noteOn (16, e.pitch, (juce::uint8) juce::jlimit (1, 127, e.velocity))
                                         : juce::MidiMessage::noteOff (16, e.pitch), offset);
            active[(size_t) e.pitch] = e.velocity > 0;
        }
        elapsed = end; position.store (end);
        if (next >= count) { releaseNotes (midi, samples - 1); playing.store (false); }
    }
private:
    struct Event { double seconds = 0.0; int pitch = 60, velocity = 0; };
    static constexpr int capacity = MelodyClip::maxNotes * 2;
    std::vector<Event> pending, events;
    std::array<bool, 128> active {};
    juce::SpinLock commandLock;
    std::atomic<bool> commandReady { false }, playing { false };
    std::atomic<double> position { 0.0 };
    int pendingCount = 0, count = 0, next = 0;
    bool pendingStart = false;
    double elapsed = 0.0;

    struct PendingSequencerNoteOff
    {
        int midiNote = 60;
        int samplesUntil = 0;
        bool active = false;
    };
    static constexpr int sequencerTriggerCapacity = 512;
    static constexpr int sequencerNoteOffCapacity = 512;
    RetroMatchSequencer::Core sequencer;
    RetroMatchSequencer::Settings pendingSequencerSettings {};
    RetroMatchSequencer::Settings appliedSequencerSettings {};
    std::array<RetroMatchSequencer::Step, RetroMatchSequencer::maxSteps> pendingSequencerSteps {};
    std::array<RetroMatchSequencer::Trigger, sequencerTriggerCapacity> sequencerTriggers {};
    std::array<PendingSequencerNoteOff, sequencerNoteOffCapacity> sequencerNoteOffs {};
    std::array<int, 128> activeSequencerNotes {};
    juce::MidiBuffer sequencerInput;
    mutable juce::SpinLock sequencerCommandLock;
    std::atomic<bool> sequencerCommandReady { true };
    std::atomic<int> sequencerCurrentStep { 0 };
    std::atomic<bool> sequencerRunning { false };
    bool pendingSequencerPatternDirty = true;
    bool appliedSequencerEnabled = false;
    double sequencerSampleRate = 0.0;

    void releaseNotes (juce::MidiBuffer& midi, int offset)
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            if (active[(size_t) pitch])
            { midi.addEvent (juce::MidiMessage::noteOff (16, pitch), offset); active[(size_t) pitch] = false; }
    }
    void releaseSequencerNotes (juce::MidiBuffer& midi, int offset)
    {
        for (int pitch = 0; pitch < 128; ++pitch)
        {
            if (activeSequencerNotes[(size_t) pitch] > 0)
                midi.addEvent (juce::MidiMessage::noteOff (1, pitch), offset);
            activeSequencerNotes[(size_t) pitch] = 0;
        }
        for (auto& pendingOff : sequencerNoteOffs) pendingOff.active = false;
        sequencer.allNotesOff (true);
    }
    void applySequencerCommands (juce::MidiBuffer& midi, double sr)
    {
        if (std::abs (sequencerSampleRate - sr) > 0.5)
        {
            sequencer.prepare (sr);
            sequencerSampleRate = sr;
        }
        if (! sequencerCommandReady.load (std::memory_order_acquire)) return;
        const juce::SpinLock::ScopedTryLockType guard (sequencerCommandLock);
        if (! guard.isLocked()) return;

        const bool wasEnabled = appliedSequencerEnabled;
        appliedSequencerSettings = pendingSequencerSettings;
        sequencer.setSettings (appliedSequencerSettings);
        if (pendingSequencerPatternDirty)
        {
            for (int i = 0; i < RetroMatchSequencer::maxSteps; ++i)
                sequencer.setStep (i, pendingSequencerSteps[(size_t) i]);
            pendingSequencerPatternDirty = false;
        }
        appliedSequencerEnabled = appliedSequencerSettings.enabled;
        if (wasEnabled && ! appliedSequencerEnabled)
            releaseSequencerNotes (midi, 0);
        sequencerCommandReady.store (false, std::memory_order_release);
    }
    void drainSequencerNoteOffs (juce::MidiBuffer& midi, int samples)
    {
        for (auto& pendingOff : sequencerNoteOffs)
        {
            if (! pendingOff.active) continue;
            if (pendingOff.samplesUntil < samples)
            {
                const int offset = juce::jlimit (0, samples - 1, pendingOff.samplesUntil);
                midi.addEvent (juce::MidiMessage::noteOff (1, pendingOff.midiNote), offset);
                auto& countForPitch = activeSequencerNotes[(size_t) pendingOff.midiNote];
                countForPitch = juce::jmax (0, countForPitch - 1);
                pendingOff.active = false;
            }
            else pendingOff.samplesUntil -= samples;
        }
    }
    void scheduleSequencerNoteOff (int midiNote, int samplesUntil)
    {
        for (auto& pendingOff : sequencerNoteOffs)
            if (! pendingOff.active)
            {
                pendingOff.midiNote = midiNote;
                pendingOff.samplesUntil = juce::jmax (0, samplesUntil);
                pendingOff.active = true;
                return;
            }
    }
    void processSequencer (juce::MidiBuffer& midi, int samples)
    {
        if (! appliedSequencerEnabled)
        {
            sequencerRunning.store (false, std::memory_order_relaxed);
            return;
        }

        sequencerInput.clear();
        sequencerInput.addEvents (midi, 0, samples, 0);
        midi.clear();
        for (const auto metadata : sequencerInput)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                sequencer.noteOn (message.getNoteNumber(), message.getFloatVelocity());
                continue;
            }
            if (message.isNoteOff())
            {
                sequencer.noteOff (message.getNoteNumber());
                continue;
            }
            if (message.isAllNotesOff())
            {
                sequencer.allNotesOff (true);
                releaseSequencerNotes (midi, metadata.samplePosition);
            }
            midi.addEvent (message, metadata.samplePosition);
        }

        drainSequencerNoteOffs (midi, samples);
        RetroMatchSequencer::Transport transport;
        transport.playing = true;
        transport.bpm = appliedSequencerSettings.internalBpm;
        const int triggerCount = sequencer.processBlock (samples, transport, sequencerTriggers.data(), sequencerTriggerCapacity);
        for (int i = 0; i < triggerCount; ++i)
        {
            const auto& trigger = sequencerTriggers[(size_t) i];
            const auto velocity = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (trigger.velocity * 127.0f));
            midi.addEvent (juce::MidiMessage::noteOn (1, trigger.midiNote, velocity), trigger.sampleOffset);
            ++activeSequencerNotes[(size_t) trigger.midiNote];
            const int noteOffOffset = trigger.sampleOffset + juce::jmax (1, trigger.durationSamples);
            if (noteOffOffset < samples)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, trigger.midiNote), noteOffOffset);
                auto& countForPitch = activeSequencerNotes[(size_t) trigger.midiNote];
                countForPitch = juce::jmax (0, countForPitch - 1);
            }
            else scheduleSequencerNoteOff (trigger.midiNote, noteOffOffset - samples);
        }
        sequencerCurrentStep.store (sequencer.getCurrentStep(), std::memory_order_relaxed);
        sequencerRunning.store (true, std::memory_order_relaxed);
    }
};