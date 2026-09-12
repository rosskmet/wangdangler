// Source/DSP/Saturation/MultiBandSaturator.h
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include "../Oversampling/OversamplingEngine.h"

namespace mbsc {

class MultiBandSaturator
{
public:
    static constexpr int numBands = 3;

    void prepare(double sampleRate, int maxBlockSize, int numChannels, int bands)
    {
        (void) bands;
        for (int b = 0; b < numBands; ++b)
        {
            if (! engines[b]) engines[b] = std::make_unique<OversamplingEngine>();
            engines[b]->prepare(sampleRate, maxBlockSize, numChannels);
        }
    }

    void reset() { for (auto& e : engines) if (e) e->reset(); }

    void setOversamplingFactor(OversamplingEngine::Factor f)
    {
        for (auto& e : engines) if (e) e->setFactor(f);
    }

    // Pass-through until the waveshaper models land
    void processBand(int /*band*/, juce::AudioBuffer<float>& /*buffer*/) {}

    int getTotalLatencySamples() const
    {
        int maxLatency = 0;
        for (auto& e : engines)
            if (e) maxLatency = juce::jmax(maxLatency, e->getLatencySamples());
        return maxLatency;   // uniform across bands: report the max
    }

private:
    std::array<std::unique_ptr<OversamplingEngine>, numBands> engines;
};

} // namespace mbsc