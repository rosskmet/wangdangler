#include "CrossoverManager.h"

namespace mbsc {

CrossoverManager::CrossoverManager(int numBands)
    : numBands(numBands)
{
    jassert(numBands == 3);  // For this project, we use 3 bands
    smoothedLowFreq.reset(0.01);  // 10ms smoothing
    smoothedHighFreq.reset(0.01);
}

CrossoverManager::~CrossoverManager() = default;

void CrossoverManager::prepare(double sampleRate, int blockSize, int numBands)
{
    this->sampleRate = sampleRate;
    this->blockSize = blockSize;

    smoothedLowFreq .reset(sampleRate, 0.01);   // 10 ms ramp
    smoothedHighFreq.reset(sampleRate, 0.01);
    smoothedLowFreq .setCurrentAndTargetValue(crossoverLowFreq);
    smoothedHighFreq.setCurrentAndTargetValue(crossoverHighFreq);

    allocateBandBuffers();
    rebuildFilterChain();
}

void CrossoverManager::allocateBandBuffers()
{
    bandBuffers.clear();
    for (int i = 0; i < numBands; ++i)
    {
        juce::AudioBuffer<float> buffer(1, blockSize * 2);  // Extra for lookahead
        buffer.clear();
        bandBuffers.push_back(std::move(buffer));
    }
}

void CrossoverManager::rebuildFilterChain()
{
    // Destroy existing filters
    iirFilters.reset();
    firFilters.reset();
    
    if (filterType == FilterType::IIR)
    {
        iirFilters = std::make_unique<IIRFilterBank>(numBands);
        iirFilters->prepare(sampleRate, blockSize);
        iirFilters->setCrossoverFrequencies(crossoverLowFreq, crossoverHighFreq);
        iirFilters->setOrder(filterOrder);
    }
    else
    {
        firFilters = std::make_unique<FIRFilterBank>();
        firFilters->prepare(sampleRate, blockSize, 2);   // was (sampleRate, blockSize)
        firFilters->setCrossoverFrequencies(crossoverLowFreq, crossoverHighFreq);
        firFilters->setTapCount(1024);
    }
}

void CrossoverManager::reset()
{
    if (iirFilters) iirFilters->reset();
    if (firFilters) firFilters->reset();
    
    for (auto& buffer : bandBuffers)
        buffer.clear();
}

void CrossoverManager::split(juce::AudioBuffer<float>& buffer)
{
    // Work with the input buffer directly for efficiency
    // Copy to band buffers for parallel processing
    
    auto numChannels = buffer.getNumChannels();
    auto numSamples = buffer.getNumSamples();
    
    // Ensure band buffers have enough samples
    for (auto& b : bandBuffers)
        b.setSize(1, numSamples, true, false, true);
    
    if (filterType == FilterType::IIR && iirFilters)
    {
        iirFilters->split(buffer, bandBuffers);
    }
    else if (filterType == FilterType::FIR && firFilters)
    {
        firFilters->split(buffer, bandBuffers);
    }
}

void CrossoverManager::split(const juce::AudioBuffer<float>& input,
                            std::vector<juce::AudioBuffer<float>>& bandBuffers)
{
    if (bandBuffers.size() != static_cast<size_t>(numBands))
        return;
    
    if (filterType == FilterType::IIR && iirFilters)
    {
        iirFilters->split(input, bandBuffers);
    }
    else if (filterType == FilterType::FIR && firFilters)
    {
        firFilters->split(input, bandBuffers);
    }
}

void CrossoverManager::merge(juce::AudioBuffer<float>& buffer)
{
    // Sum all band buffers back into the main buffer
    
    if (filterType == FilterType::IIR && iirFilters)
    {
        iirFilters->merge(bandBuffers, buffer);
    }
    else if (filterType == FilterType::FIR && firFilters)
    {
        firFilters->merge(bandBuffers, buffer);
    }
}

void CrossoverManager::merge(const std::vector<juce::AudioBuffer<float>>& bandBuffers,
                            juce::AudioBuffer<float>& output)
{
    output.clear();
    
    for (const auto& band : bandBuffers)
        output.addFrom(0, 0, band, 0, 0, band.getNumSamples(), 1.0f);
}

void CrossoverManager::setCrossoverFrequency(int band, float frequencyHz)
{
    jassert(band >= 0 && band < numBands - 1);

    bool changed = false;

    if (band == 0 && frequencyHz != crossoverLowFreq)
    {
        frequencyHz = juce::jlimit(MIN_CROSSOVER_FREQ,
                                   juce::jmax(MIN_CROSSOVER_FREQ,
                                              crossoverHighFreq - 100.0f),
                                   frequencyHz);
        crossoverLowFreq = frequencyHz;
        changed = true;
    }
    else if (band == 1 && frequencyHz != crossoverHighFreq)
    {
        frequencyHz = juce::jlimit(juce::jmin(MAX_CROSSOVER_FREQ,
                                              crossoverLowFreq + 100.0f),
                                   MAX_CROSSOVER_FREQ,
                                   frequencyHz);
        crossoverHighFreq = frequencyHz;
        changed = true;
    }

    if (changed)
    {
        if (useSmoothTransitions)
        {
            applySmoothing(crossoverLowFreq, crossoverHighFreq);

            if (iirFilters)
            {
                iirFilters->enableSmoothTransitions(true, transitionDuration);
                iirFilters->setCrossoverFrequencies(crossoverLowFreq,
                                                    crossoverHighFreq);
            }
        }
        else
        {
            // Immediate change (may click on large jumps)
            if (iirFilters)
                iirFilters->setCrossoverFrequencies(crossoverLowFreq,
                                                    crossoverHighFreq);
            if (firFilters)
                firFilters->setCrossoverFrequencies(crossoverLowFreq,
                                                    crossoverHighFreq);
        }
    }
}

void CrossoverManager::applySmoothing(float newFreqLow, float newFreqHigh)
{
    // Gradually transition filter coefficients to prevent clicks.
    // setCurrentAndTargetValue: snap the smoother to the new boundary
    // NOW and aim for it over the configured ramp — the IIRFilterBank
    // re-derives coefficients per-block via applySmoothedFrequencies(),
    // so the glide happens there, not here.
    smoothedLowFreq .setCurrentAndTargetValue(newFreqLow);
    smoothedHighFreq.setCurrentAndTargetValue(newFreqHigh);
}

void CrossoverManager::setFilterType(FilterType type)
{
    if (filterType != type)
    {
        filterType = type;
        rebuildFilterChain();
        
        // Preserve current crossover frequencies
        if (type == FilterType::IIR && iirFilters)
        {
            iirFilters->setCrossoverFrequencies(crossoverLowFreq, crossoverHighFreq);
        }
        else if (type == FilterType::FIR && firFilters)
        {
            firFilters->setCrossoverFrequencies(crossoverLowFreq, crossoverHighFreq);
        }
    }
}

void CrossoverManager::setFilterOrder(int order)
{
    if (order != filterOrder)
    {
        filterOrder = order;
        if (iirFilters)
        {
            iirFilters->setOrder(order);
        }
    }
}

float CrossoverManager::getCrossoverFrequency(int band) const
{
    if (band == 0) return crossoverLowFreq;
    if (band == 1) return crossoverHighFreq;
    return 0.0f;
}

juce::AudioBuffer<float>& CrossoverManager::getBandBuffer(int band)
{
    jassert(band >= 0 && band < numBands);
    return bandBuffers[band];
}

const juce::AudioBuffer<float>& CrossoverManager::getBandBuffer(int band) const
{
    jassert(band >= 0 && band < numBands);
    return bandBuffers[band];
}

int CrossoverManager::getLatencySamples() const
{
    if (filterType == FilterType::FIR)
    {
        return firLatencySamples;
    }
    return 0;  // IIR has negligible latency for this application
}

} // namespace mbsc