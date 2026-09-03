#include "SampleAnalyzer.h"
#include <cmath>
#include <numeric>

namespace
{
struct PitchEstimate { float hz = 0.0f; float confidence = 0.0f; };

PitchEstimate estimateFundamentalAutocorrelation (const float* x, int n, double sr)
{
    PitchEstimate out;
    if (n < 512 || sr <= 0.0) return out;

    const int decimation = juce::jmax (1, (int) std::floor (sr / 12000.0));
    const double reducedSr = sr / decimation;
    const int reducedN = juce::jmin (n / decimation, 24000);
    if (reducedN < 256) return out;

    std::vector<float> y ((size_t) reducedN);
    double mean = 0.0;
    for (int i = 0; i < reducedN; ++i) { y[(size_t) i] = x[i * decimation]; mean += y[(size_t) i]; }
    mean /= reducedN;
    for (auto& v : y) v -= (float) mean;

    const int minLag = juce::jmax (1, (int) (reducedSr / 1800.0));
    const int maxLag = juce::jmin (reducedN / 2, (int) (reducedSr / 35.0));
    double best = 0.0;
    int bestLag = 0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double c = 0.0, e1 = 1.0e-12, e2 = 1.0e-12;
        const int count = reducedN - lag;
        for (int i = 0; i < count; ++i)
        {
            const auto a = y[(size_t) i];
            const auto b = y[(size_t) (i + lag)];
            c += a * b; e1 += a * a; e2 += b * b;
        }
        c /= std::sqrt (e1 * e2);
        if (c > best) { best = c; bestLag = lag; }
    }

    if (bestLag > 0)
    {
        out.hz = (float) (reducedSr / bestLag);
        out.confidence = juce::jlimit (0.0f, 1.0f, (float) best);
    }
    return out;
}


float estimateConfidenceAtFundamental (const float* x, int n, double sr, float hz)
{
    if (n < 128 || hz < 20.0f || hz > sr * 0.45) return 0.0f;
    const int lag = juce::jmax (1, (int) std::round (sr / hz));
    const int count = juce::jmin (n - lag, (int) (sr * 0.35));
    if (count < 64) return 0.0f;
    double c = 0.0, e1 = 1.0e-12, e2 = 1.0e-12;
    for (int i = 0; i < count; ++i)
    {
        const double a = x[i], b = x[i + lag];
        c += a * b; e1 += a * a; e2 += b * b;
    }
    return juce::jlimit (0.0f, 1.0f, (float) (c / std::sqrt (e1 * e2)));
}

void analyseEnvelope (const float* x, int n, double sr, SoundFeatures& f)
{
    if (n <= 0 || sr <= 0.0) return;
    const int window = juce::jmax (16, (int) (sr * 0.010));
    const int hop = juce::jmax (8, window / 2);
    const int frames = juce::jmax (1, (n - window) / hop + 1);
    std::vector<float> envelope ((size_t) frames, 0.0f);

    float maxEnv = 0.0f;
    int peakFrame = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        const int start = frame * hop;
        const int count = juce::jmin (window, n - start);
        double sum = 0.0;
        for (int i = 0; i < count; ++i) sum += (double) x[start + i] * x[start + i];
        const float value = count > 0 ? (float) std::sqrt (sum / count) : 0.0f;
        envelope[(size_t) frame] = value;
        if (value > maxEnv) { maxEnv = value; peakFrame = frame; }
    }

    if (maxEnv <= 1.0e-7f) return;

    int attackFrame = peakFrame;
    for (int i = 0; i <= peakFrame; ++i)
        if (envelope[(size_t) i] >= maxEnv * 0.90f) { attackFrame = i; break; }
    f.attackSeconds = attackFrame * hop / (float) sr;

    const int sustainStart = juce::jlimit (0, frames - 1, (int) (frames * 0.45f));
    const int sustainEnd = juce::jlimit (sustainStart + 1, frames, (int) (frames * 0.72f));
    double sustainSum = 0.0;
    for (int i = sustainStart; i < sustainEnd; ++i) sustainSum += envelope[(size_t) i];
    const float sustainAbs = (float) (sustainSum / juce::jmax (1, sustainEnd - sustainStart));
    f.sustainLevel = juce::jlimit (0.0f, 1.0f, sustainAbs / maxEnv);

    const float decayTarget = juce::jmax (sustainAbs * 1.12f, maxEnv * 0.60f);
    int decayEnd = peakFrame;
    for (int i = peakFrame; i < frames; ++i)
        if (envelope[(size_t) i] <= decayTarget) { decayEnd = i; break; }
    f.decaySeconds = juce::jlimit (0.005f, 5.0f, (decayEnd - peakFrame) * hop / (float) sr);

    int lastAudible = frames - 1;
    const float releaseThreshold = maxEnv * 0.08f;
    while (lastAudible > 0 && envelope[(size_t) lastAudible] < releaseThreshold) --lastAudible;
    f.releaseSeconds = juce::jlimit (0.005f, 8.0f, (frames - 1 - lastAudible) * hop / (float) sr);

    const float crest = f.rms > 1.0e-7f ? juce::jlimit (1.0f, 12.0f, f.peak / f.rms) : 1.0f;
    const float attackTerm = 1.0f - juce::jlimit (0.0f, 1.0f, f.attackSeconds / 0.22f);
    const float crestTerm = juce::jlimit (0.0f, 1.0f, (crest - 1.5f) / 5.0f);
    f.transientScore = juce::jlimit (0.0f, 1.0f, attackTerm * 0.75f + crestTerm * 0.25f);
}

void makeWaveformPreview (const float* x, int n, SoundFeatures& f)
{
    if (n <= 0) return;
    for (int p = 0; p < SoundFeatures::waveformPointCount; ++p)
    {
        const int start = (int) ((int64_t) p * n / SoundFeatures::waveformPointCount);
        const int end = juce::jmax (start + 1, (int) ((int64_t) (p + 1) * n / SoundFeatures::waveformPointCount));
        double sum = 0.0;
        for (int i = start; i < juce::jmin (end, n); ++i) sum += x[i];
        f.waveformPreview[(size_t) p] = (float) (sum / juce::jmax (1, end - start));
    }
    float mag = 0.0f;
    for (auto v : f.waveformPreview) mag = juce::jmax (mag, std::abs (v));
    if (mag > 1.0e-6f) for (auto& v : f.waveformPreview) v /= mag;
}
}


void analyseTemporalSpectrum (const float* x, int n, double sr, SoundFeatures& f)
{
    if (n < 64 || sr <= 0.0) return;

    int order = 11;
    while ((1 << order) > n && order > 8) --order;
    const int fftN = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) fftN, juce::dsp::WindowingFunction<float>::hann, true);
    juce::HeapBlock<float> data ((size_t) fftN * 2);

    const double minHz = 40.0;
    const double maxHz = juce::jmin (20000.0, sr * 0.48);
    float maxRms = 0.0f;

    for (int frame = 0; frame < SoundFeatures::temporalFrameCount; ++frame)
    {
        const double t = SoundFeatures::temporalFrameCount > 1 ? frame / (double) (SoundFeatures::temporalFrameCount - 1) : 0.0;
        const int centre = (int) std::round (t * juce::jmax (0, n - 1));
        const int start = juce::jlimit (0, juce::jmax (0, n - fftN), centre - fftN / 2);
        const int copyCount = juce::jmin (fftN, n - start);

        juce::FloatVectorOperations::clear (data, fftN * 2);
        juce::FloatVectorOperations::copy (data, x + start, copyCount);
        window.multiplyWithWindowingTable (data, (size_t) fftN);
        fft.performFrequencyOnlyForwardTransform (data);

        double rmsSum = 0.0;
        for (int i = 0; i < copyCount; ++i) rmsSum += (double) x[start + i] * x[start + i];
        const float frameRms = copyCount > 0 ? (float) std::sqrt (rmsSum / copyCount) : 0.0f;
        f.temporalRms[(size_t) frame] = frameRms;
        maxRms = juce::jmax (maxRms, frameRms);

        const int bins = fftN / 2;
        float maxBand = 0.0f;
        for (int band = 0; band < SoundFeatures::temporalBandCount; ++band)
        {
            const double t0 = band / (double) SoundFeatures::temporalBandCount;
            const double t1 = (band + 1) / (double) SoundFeatures::temporalBandCount;
            const double hz0 = minHz * std::pow (maxHz / minHz, t0);
            const double hz1 = minHz * std::pow (maxHz / minHz, t1);
            const int b0 = juce::jlimit (1, bins - 1, (int) std::floor (hz0 * fftN / sr));
            const int b1 = juce::jlimit (b0 + 1, bins, (int) std::ceil (hz1 * fftN / sr));
            double energy = 0.0;
            for (int b = b0; b < b1; ++b) energy += data[b];
            const float value = std::log1p ((float) energy);
            f.temporalSpectralBands[(size_t) frame][(size_t) band] = value;
            maxBand = juce::jmax (maxBand, value);
        }
        if (maxBand > 1.0e-6f)
            for (auto& value : f.temporalSpectralBands[(size_t) frame]) value /= maxBand;
    }

    if (maxRms > 1.0e-7f)
        for (auto& value : f.temporalRms) value /= maxRms;

    double motion = 0.0;
    int comparisons = 0;
    for (int frame = 1; frame < SoundFeatures::temporalFrameCount; ++frame)
        for (int band = 0; band < SoundFeatures::temporalBandCount; ++band)
        {
            motion += std::abs (f.temporalSpectralBands[(size_t) frame][(size_t) band]
                              - f.temporalSpectralBands[(size_t) (frame - 1)][(size_t) band]);
            ++comparisons;
        }
    f.spectralMotion = comparisons > 0 ? juce::jlimit (0.0f, 1.0f, (float) (motion / comparisons) * 2.0f) : 0.0f;
}

void makeTimbreCepstrum (SoundFeatures& f)
{
    // DCT-II over the log-spaced global spectrum. This is intentionally a lightweight
    // timbre-envelope descriptor rather than a speech-specific MFCC implementation.
    std::array<float, SoundFeatures::cepstralCount> raw {};
    for (int k = 0; k < SoundFeatures::cepstralCount; ++k)
    {
        double sum = 0.0;
        for (int n = 0; n < SoundFeatures::spectralBandCount; ++n)
        {
            const double logValue = std::log (1.0e-4 + f.spectralBands[(size_t) n]);
            sum += logValue * std::cos (juce::MathConstants<double>::pi * (n + 0.5) * k / SoundFeatures::spectralBandCount);
        }
        raw[(size_t) k] = (float) sum;
    }

    double norm = 1.0e-9;
    for (int k = 1; k < SoundFeatures::cepstralCount; ++k) norm += raw[(size_t) k] * raw[(size_t) k];
    norm = std::sqrt (norm);
    f.timbreCepstrum[0] = juce::jlimit (-1.0f, 1.0f, raw[0] / 80.0f);
    for (int k = 1; k < SoundFeatures::cepstralCount; ++k)
        f.timbreCepstrum[(size_t) k] = (float) (raw[(size_t) k] / norm);
}

std::optional<SoundFeatures> SampleAnalyzer::analyzeFile (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader) return std::nullopt;

    const auto maxSamples = (int64_t) juce::jmin<double> ((double) reader->lengthInSamples, reader->sampleRate * 12.0);
    const int sampleCount = (int) maxSamples;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> decoded (channels, sampleCount);
    if (! reader->read (&decoded, 0, sampleCount, 0, true, true)) return std::nullopt;

    return analyzeBuffer (decoded, reader->sampleRate);
}

SoundFeatures SampleAnalyzer::analyzeBuffer (const juce::AudioBuffer<float>& audio, double sr, float expectedFundamentalHz)
{
    SoundFeatures f;
    f.sampleRate = sr;
    const int n = audio.getNumSamples();
    if (n <= 0 || audio.getNumChannels() <= 0 || sr <= 0.0) return f;
    f.duration = n / (float) sr;

    juce::AudioBuffer<float> monoMix;
    const juce::AudioBuffer<float>* analysis = &audio;
    if (audio.getNumChannels() > 1)
    {
        monoMix.setSize (1, n);
        monoMix.clear();
        const int channels = audio.getNumChannels();
        for (int ch = 0; ch < channels; ++ch)
            monoMix.addFrom (0, 0, audio, ch, 0, n, 1.0f / channels);
        analysis = &monoMix;

        const auto* l = audio.getReadPointer (0);
        const auto* r = audio.getReadPointer (1);
        double midEnergy = 1.0e-12, sideEnergy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double mid = 0.5 * (l[i] + r[i]);
            const double side = 0.5 * (l[i] - r[i]);
            midEnergy += mid * mid;
            sideEnergy += side * side;
        }
        f.stereoWidth = juce::jlimit (0.0f, 1.0f, (float) std::sqrt (sideEnergy / midEnergy));
    }

    const auto* x = analysis->getReadPointer (0);
    f.rms = analysis->getRMSLevel (0, 0, n);
    f.peak = analysis->getMagnitude (0, n);
    int zc = 0;
    for (int i = 1; i < n; ++i) if ((x[i - 1] >= 0.0f) != (x[i] >= 0.0f)) ++zc;
    f.zeroCrossingRate = n > 1 ? (float) zc / (float) (n - 1) : 0.0f;

    if (expectedFundamentalHz > 20.0f)
    {
        f.fundamentalHz = expectedFundamentalHz;
        f.pitchConfidence = estimateConfidenceAtFundamental (x, n, sr, expectedFundamentalHz);
    }
    else
    {
        const int pitchN = juce::jmin (n, (int) (sr * 1.5));
        const auto pitch = estimateFundamentalAutocorrelation (x, pitchN, sr);
        f.fundamentalHz = pitch.hz;
        f.pitchConfidence = pitch.confidence;
    }
    f.harmonicity = f.pitchConfidence;

    int order = 14;
    while ((1 << order) > n && order > 8) --order;
    const int fftN = 1 << order;
    juce::dsp::FFT fft (order);
    juce::HeapBlock<float> data ((size_t) fftN * 2);
    juce::FloatVectorOperations::clear (data, fftN * 2);
    juce::dsp::WindowingFunction<float> window ((size_t) fftN, juce::dsp::WindowingFunction<float>::hann, true);

    const int copyCount = juce::jmin (fftN, n);
    juce::FloatVectorOperations::copy (data, x, copyCount);
    window.multiplyWithWindowingTable (data, (size_t) fftN);
    fft.performFrequencyOnlyForwardTransform (data);

    const int bins = fftN / 2;
    double sum = 1.0e-12, weighted = 0.0;
    double logSum = 0.0, maxMagnitude = 0.0;
    for (int i = 1; i < bins; ++i)
    {
        const double mag = juce::jmax (1.0e-12f, data[i]);
        const double hz = i * sr / fftN;
        sum += mag;
        weighted += mag * hz;
        logSum += std::log (mag);
        maxMagnitude = juce::jmax (maxMagnitude, mag);
    }
    f.spectralCentroidHz = (float) (weighted / sum);
    const double arithmetic = sum / juce::jmax (1, bins - 1);
    const double geometric = std::exp (logSum / juce::jmax (1, bins - 1));
    f.spectralFlatness = juce::jlimit (0.0f, 1.0f, (float) (geometric / juce::jmax (1.0e-12, arithmetic)));

    double secondMoment = 0.0, cumulative = 0.0;
    double lowEnergy = 0.0, highEnergy = 0.0;
    for (int i = 1; i < bins; ++i)
    {
        const double mag = data[i];
        const double hz = i * sr / fftN;
        const double d = hz - f.spectralCentroidHz;
        secondMoment += mag * d * d;
        if (hz < 500.0) lowEnergy += mag;
        if (hz > 5000.0) highEnergy += mag;
        cumulative += mag;
        if (f.spectralRolloffHz <= 0.0f && cumulative >= sum * 0.85)
            f.spectralRolloffHz = (float) hz;
    }
    f.spectralBandwidthHz = (float) std::sqrt (secondMoment / sum);
    f.lowEnergyRatio = juce::jlimit (0.0f, 1.0f, (float) (lowEnergy / sum));
    f.highEnergyRatio = juce::jlimit (0.0f, 1.0f, (float) (highEnergy / sum));

    if (f.fundamentalHz > 20.0f)
    {
        double odd = 0.0, even = 0.0;
        for (int harmonic = 1; harmonic <= 20; ++harmonic)
        {
            const double hz = f.fundamentalHz * harmonic;
            if (hz >= sr * 0.48) break;
            const int bin = juce::jlimit (1, bins - 1, (int) std::round (hz * fftN / sr));
            const double mag = data[bin];
            (harmonic % 2 == 1 ? odd : even) += mag;
        }
        f.oddHarmonicRatio = (float) (odd / juce::jmax (1.0e-12, odd + even));

        double peakWeight = 1.0e-12, weightedError = 0.0;
        const double threshold = maxMagnitude * 0.03;
        for (int bin = 1; bin < bins; ++bin)
        {
            const double mag = data[bin];
            if (mag < threshold) continue;
            const double hz = bin * sr / fftN;
            const int harmonic = juce::jmax (1, (int) std::round (hz / f.fundamentalHz));
            const double nearest = harmonic * f.fundamentalHz;
            const double normalizedError = juce::jlimit (0.0, 1.0, std::abs (hz - nearest) / juce::jmax (1.0, f.fundamentalHz * 0.5));
            weightedError += mag * normalizedError;
            peakWeight += mag;
        }
        f.inharmonicity = juce::jlimit (0.0f, 1.0f, (float) (weightedError / peakWeight));
    }

    const double minHz = 40.0;
    const double maxHz = juce::jmin (20000.0, sr * 0.48);
    float maxBand = 0.0f;
    for (int band = 0; band < SoundFeatures::spectralBandCount; ++band)
    {
        const double t0 = (double) band / SoundFeatures::spectralBandCount;
        const double t1 = (double) (band + 1) / SoundFeatures::spectralBandCount;
        const double hz0 = minHz * std::pow (maxHz / minHz, t0);
        const double hz1 = minHz * std::pow (maxHz / minHz, t1);
        const int b0 = juce::jlimit (1, bins - 1, (int) std::floor (hz0 * fftN / sr));
        const int b1 = juce::jlimit (b0 + 1, bins, (int) std::ceil (hz1 * fftN / sr));
        double energy = 0.0;
        for (int b = b0; b < b1; ++b) energy += data[b];
        const float value = std::log1p ((float) energy);
        f.spectralBands[(size_t) band] = value;
        maxBand = juce::jmax (maxBand, value);
    }
    if (maxBand > 1.0e-6f) for (auto& band : f.spectralBands) band /= maxBand;

    analyseTemporalSpectrum (x, n, sr, f);
    makeTimbreCepstrum (f);
    analyseEnvelope (x, n, sr, f);
    makeWaveformPreview (x, n, f);
    return f;
}
