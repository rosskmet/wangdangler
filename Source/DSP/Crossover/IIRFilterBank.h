#pragma once

#include <juce_dsp/juce_dsp.h>

namespace mbsc {

/**
 * LR4 3-band crossover built on juce::dsp::LinkwitzRileyFilter.
 *
 * Topology (per sample, one cascade computes all three bands):
 *   x ──► LR(f1) ──┬── low
 *                  └── upper ──► LR(f2) ──┬── mid (polarity-inverted)
 *                                         └── high
 *
 * Frequency changes glide over 10 ms via SmoothedValue pushed per block.
 * Latency: zero (all filter state is causal IIR).
 */
class IIRFilterBank
{
public:
    IIRFilterBank() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setCrossoverFrequencies (float lowFreq, float highFreq);

    void split (const juce::AudioBuffer<float>& input,
                std::vector<juce::AudioBuffer<float>>& bandBuffers);

    void merge (const std::vector<juce::AudioBuffer<float>>& bandBuffers,
                juce::AudioBuffer<float>& output);

    int getLatencySamples() const { return 0; }

private:
    static constexpr int maxChannels = 2;

    juce::dsp::LinkwitzRileyFilter<float> lowEdge;   // @ f1
    juce::dsp::LinkwitzRileyFilter<float> highEdge; // @ f2

    juce::SmoothedValue<float> smoothedLowFreq  { 200.0f };
    juce::SmoothedValue<float> smoothedHighFreq { 2000.0f };

    double sampleRate = 48000.0;   // replaced in prepare() — see P1 audit
};

} // namespace mbsc