#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "DSPConstants.h"
#include "LinkwitzRileyFilters.h"
#include "IIRFilterBank.h"
#include "FIRFilterBank.h"

namespace mbsc {

enum class FilterType { IIR, FIR };

class CrossoverManager
{
public:
    explicit CrossoverManager(int numBands = 3);
    ~CrossoverManager();

    void prepare(double sampleRate, int blockSize, int numBands);
    void reset();

    // Split input into bands (process in-place or to destination buffer)
    void split(juce::AudioBuffer<float>& buffer);
    void split(const juce::AudioBuffer<float>& input, 
               std::vector<juce::AudioBuffer<float>>& bandBuffers);

    // Merge bands back together
    void merge(juce::AudioBuffer<float>& buffer);
    void merge(const std::vector<juce::AudioBuffer<float>>& bandBuffers,
               juce::AudioBuffer<float>& output);

    // Configuration
    void setCrossoverFrequency(int band, float frequencyHz);
    void setFilterType(FilterType type);
    void setFilterOrder(int order);  // For IIR: 2nd/4th/6th, For FIR: tap count
    
    // Accessors
    float getCrossoverFrequency(int band) const;
    FilterType getFilterType() const { return filterType; }
    int getNumBands() const { return numBands; }
    
    // Get band-specific buffers (for processing)
    juce::AudioBuffer<float>& getBandBuffer(int band);
    const juce::AudioBuffer<float>& getBandBuffer(int band) const;
    
    // Latency (important for compensation)
    int getLatencySamples() const;

private:
    void rebuildFilterChain();
    void allocateBandBuffers();
    void applySmoothing(float newFreqLow, float newFreqHigh);

    // State
    int numBands = 3;
    double sampleRate = 48000.0;
    int blockSize = 512;
    FilterType filterType = FilterType::IIR;
    int filterOrder = 4;  // 4th order Linkwitz-Riley
    
    // Crossover frequencies (between bands)
    float crossoverLowFreq = 200.0f;   // Low/Mid boundary
    float crossoverHighFreq = 2000.0f; // Mid/High boundary
    
    // Smoothed frequency parameters (prevent clicks on change)
    juce::SmoothedValue<float> smoothedLowFreq;
    juce::SmoothedValue<float> smoothedHighFreq;

    // Smooth transition config (see LinkwitzRileyFilters' FilterTransitionConfig)
    bool  useSmoothTransitions = false;
    float transitionDuration   = 0.01f;   // 10 ms crossfade
    
    // Filter banks
    std::unique_ptr<IIRFilterBank> iirFilters;
    std::unique_ptr<FIRFilterBank> firFilters;
    
    // Band buffers (stored for processing chain)
    std::vector<juce::AudioBuffer<float>> bandBuffers;
    
    // Delay compensation for FIR
    int firLatencySamples = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossoverManager)
};

} // namespace mbsc