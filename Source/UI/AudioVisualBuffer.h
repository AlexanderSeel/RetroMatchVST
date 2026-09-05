#pragma once
#include <JuceHeader.h>

// One producer (audio callback), one consumer (editor timer). Drop visual data
// when the editor is closed/late; audio never waits and never allocates here.
class AudioVisualBuffer
{
public:
    static constexpr int capacity = 16384;
    void push (const juce::AudioBuffer<float>& audio)
    {
        if (audio.getNumChannels() == 0) return;
        int a, n, b, m;
        fifo.prepareToWrite (juce::jmin (audio.getNumSamples(), capacity), a, n, b, m);
        const int right = juce::jmin (1, audio.getNumChannels() - 1);
        for (int i = 0; i < n + m; ++i)
        {
            const int at = i < n ? a + i : b + i - n;
            left[(size_t) at] = audio.getSample (0, i);
            rightData[(size_t) at] = audio.getSample (right, i);
        }
        fifo.finishedWrite (n + m);
    }
    int read (float* l, float* r, int maxSamples)
    {
        int a, n, b, m; fifo.prepareToRead (maxSamples, a, n, b, m);
        for (int i = 0; i < n + m; ++i)
        {
            const int at = i < n ? a + i : b + i - n;
            l[i] = left[(size_t) at]; r[i] = rightData[(size_t) at];
        }
        fifo.finishedRead (n + m); return n + m;
    }
private:
    juce::AbstractFifo fifo { capacity };
    std::array<float, capacity> left {}, rightData {};
};
