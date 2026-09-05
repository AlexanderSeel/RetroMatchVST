#pragma once
#include "../Analysis/MelodyAnalyzer.h"
#include <atomic>

// Message-thread commands, sample-accurate audio-thread scheduling. The audio
// thread never waits for the UI and owns the live transport/event storage.
class MelodyTransport
{
public:
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
    void process (juce::MidiBuffer& midi, int samples, double sr)
    {
        if (samples <= 0 || sr <= 0.0) return;
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
    std::array<Event, capacity> pending {}, events {};
    std::array<bool, 128> active {};
    juce::SpinLock commandLock;
    std::atomic<bool> commandReady { false }, playing { false };
    std::atomic<double> position { 0.0 };
    int pendingCount = 0, count = 0, next = 0;
    bool pendingStart = false;
    double elapsed = 0.0;
    void releaseNotes (juce::MidiBuffer& midi, int offset)
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            if (active[(size_t) pitch])
            { midi.addEvent (juce::MidiMessage::noteOff (16, pitch), offset); active[(size_t) pitch] = false; }
    }
};
