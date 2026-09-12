#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>

namespace mbsc {

/**
 * FIR-based linear-phase 3-band crossover.
 *
 * Coefficient construction (COMPLEMENTARY BY DESIGN — see .cpp):
 *
 *     low  = W * sinc(f1)
 *     mid  = W * sinc(f2) - W * sinc(f1)
 *     high = delta - W * sinc(f2)
 *
 * Sum of all three bands = delta exactly (windowed terms telescope),
 * so split+merge reconstructs the input with flat magnitude — the FIR
 * equivalent of the Linkwitz-Riley flat-summation property, without
 * polarity inversion.
 *
 * Trade-offs vs the IIR (Linkwitz-Riley) path:
 *   + Linear phase (constant group delay — zero phase distortion)
 *   + Exact complementary summation
 *   - Latency = (taps - 1) / 2 samples  (10.7 ms @ 1024 taps, 48 kHz)
 *   - Heavier CPU (direct convolution; FFT upgrade path noted in .cpp)
 *
 * All bands share one tap count => one shared latency => bands stay
 * phase-aligned at the summation point. Do NOT support per-band tap
 * counts without delay-aligning first.
 */
class FIRFilterBank
{
public:
    enum class Quality { Low = 256, Medium = 512, High = 1024, Ultra = 2048 };

    FIRFilterBank() = default;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setCrossoverFrequencies(float lowFreq, float highFreq);
    void setLowCutoff(float freqHz);
    void setHighCutoff(float freqHz);
    void setTapCount(int taps);
    void setQuality(Quality q);

    void split(const juce::AudioBuffer<float>& input,
               std::vector<juce::AudioBuffer<float>>& bandBuffers);

    void merge(const std::vector<juce::AudioBuffer<float>>& bandBuffers,
               juce::AudioBuffer<float>& output);

    /** Group delay of every band, in samples: (taps - 1) / 2. */
    int getLatencySamples() const { return (tapCount - 1) / 2; }

    /** Read-only coefficient access (future FrequencyView visualizer). */
    const std::vector<float>& getCoefficients(int band) const;

private:
    void generateCoefficients();

    static float sinc(float x);
    static float besselI0(float x);          // Kaiser window support

    double sampleRate = 48000.0;
    float  lowCutoff  = 200.0f;              // low/mid boundary
    float  highCutoff = 2000.0f;             // mid/high boundary
    int    tapCount   = 1024;
    int    numChannels = 2;

    // Coefficients: [band][tap]. Sized to tapCount; setTapCount is the
    // only resizer (called from UI/message thread in practice).
    std::array<std::vector<float>, 3> coeffs;

    // ONE shared input history ring — all three bands convolve the same
    // input signal, so a single delayed copy of x serves all of them.
    // Power-of-two length => index wraparound is a bitmask.
    std::vector<std::vector<float>> history;   // [channel][sample]
    int histMask = 0;
    int writePos = 0;
};

} // namespace mbsc