#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

#include "IIRFilterBank.h"
#include "FIRFilterBank.h"

namespace mbsc
{

enum class FilterType { IIR, FIR };

class CrossoverManager
{
public:
    explicit CrossoverManager (int numBands = 3);
    ~CrossoverManager();

    void prepare (double sampleRate, int blockSize, int numChannels);
    void reset();

    void split (const juce::AudioBuffer<float>& input,
                std::vector<juce::AudioBuffer<float>>& bandBuffers);

    void merge (const std::vector<juce::AudioBuffer<float>>& bandBuffers,
                juce::AudioBuffer<float>& output);

    void setCrossoverFrequency (int band, float frequencyHz);
    void setFilterType (FilterType type);

    /** Stored for API compatibility. The IIR bank currently implements
        LR4 only; LR8 (cascade) is a planned follow-up, FIR ignores it. */
    void setFilterOrder (int order);

    float getCrossoverFrequency (int band) const;
    FilterType getFilterType() const   { return filterType; }
    int getNumBands() const            { return numBands; }

    int getLatencySamples() const;

private:
    void rebuildFilterChain();

    int numBands = 3;
    double sampleRate = 48000.0;   // overwritten in prepare() from the host
    int blockSize = 512;
    int numChannels = 2;

    FilterType filterType = FilterType::IIR;
    int filterOrder = 4;           // currently decorative — see setFilterOrder

    float crossoverLowFreq  = 200.0f;   // Low/Mid boundary
    float crossoverHighFreq = 2000.0f; // Mid/High boundary

    std::unique_ptr<IIRFilterBank> iirFilters;
    std::unique_ptr<FIRFilterBank> firFilters;

    int firLatencySamples = 0;     // now actually assigned — PDC was lying before

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverManager)
};

} // namespace mbsc