#include "CrossoverManager.h"

#include <cmath>

namespace mbsc
{

CrossoverManager::CrossoverManager (int numBands)
    : numBands (numBands)
{
    jassert (numBands == 3); // this design is a 3-band topology
}

CrossoverManager::~CrossoverManager() = default;

void CrossoverManager::prepare (double sampleRate, int blockSize, int numChannels)
{
    this->sampleRate  = sampleRate;
    this->blockSize   = blockSize;
    this->numChannels = numChannels;

    rebuildFilterChain();
}

void CrossoverManager::reset()
{
    if (iirFilters) iirFilters->reset();
    if (firFilters) firFilters->reset();
}

void CrossoverManager::rebuildFilterChain()
{
    if (filterType == FilterType::IIR)
    {
        if (! iirFilters)
            iirFilters = std::make_unique<IIRFilterBank>(); // default ctor — no arg

        iirFilters->prepare (sampleRate, blockSize, numChannels);
        iirFilters->setCrossoverFrequencies (crossoverLowFreq, crossoverHighFreq);
    }
    else
    {
        if (! firFilters)
            firFilters = std::make_unique<FIRFilterBank>();

        firFilters->prepare (sampleRate, blockSize, numChannels);
        firFilters->setCrossoverFrequencies (crossoverLowFreq, crossoverHighFreq);
        firLatencySamples = firFilters->getLatencySamples(); // report REAL latency
    }
}

void CrossoverManager::setCrossoverFrequency (int band, float frequencyHz)
{
    if (band == 0)
    {
        // Clamp below the high boundary (keeps bands non-overlapping)
        frequencyHz = juce::jlimit (MIN_CROSSOVER_FREQ,
                                    juce::jmax (MIN_CROSSOVER_FREQ, crossoverHighFreq - 100.0f),
                                    frequencyHz);
        if (std::abs (frequencyHz - crossoverLowFreq) < 0.01f)
            return;
        crossoverLowFreq = frequencyHz;
    }
    else if (band == 1)
    {
        frequencyHz = juce::jlimit (juce::jmin (MAX_CROSSOVER_FREQ, crossoverLowFreq + 100.0f),
                                    MAX_CROSSOVER_FREQ,
                                    frequencyHz);
        if (std::abs (frequencyHz - crossoverHighFreq) < 0.01f)
            return;
        crossoverHighFreq = frequencyHz;
    }
    else
    {
        jassertfalse;
        return;
    }

    // Push to whichever banks exist; the active one glides over ~10 ms.
    if (iirFilters)
        iirFilters->setCrossoverFrequencies (crossoverLowFreq, crossoverHighFreq);
    if (firFilters)
        firFilters->setCrossoverFrequencies (crossoverLowFreq, crossoverHighFreq);
}

void CrossoverManager::setFilterType (FilterType type)
{
    if (filterType == type)
        return;

    filterType = type;
    rebuildFilterChain(); // new bank gets current freqs in rebuild
}

void CrossoverManager::setFilterOrder (int order)
{
    filterOrder = order;   // stored; LR4 is the only implemented order for now
}

void CrossoverManager::split (const juce::AudioBuffer<float>& input,
                              std::vector<juce::AudioBuffer<float>>& bandBuffers)
{
    if (filterType == FilterType::IIR && iirFilters)
        iirFilters->split (input, bandBuffers);
    else if (filterType == FilterType::FIR && firFilters)
        firFilters->split (input, bandBuffers);
    else
        jassertfalse; // no bank prepared — check prepare() was called
}

void CrossoverManager::merge (const std::vector<juce::AudioBuffer<float>>& bandBuffers,
                              juce::AudioBuffer<float>& output)
{
    // Deliberately NOT delegated to the banks: doing it here once, per
    // channel, covers both filter types with identical, stereo-correct
    // code. (Historical note: the old manager summed all bands into
    // channel 0 — that mono-collapse bug dies here.)
    output.clear();
    const int numSamples = output.getNumSamples();

    for (const auto& band : bandBuffers)
        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            if (ch < band.getNumChannels())
                output.addFrom (ch, 0, band, ch, 0, numSamples);
}

float CrossoverManager::getCrossoverFrequency (int band) const
{
    return (band == 0) ? crossoverLowFreq : crossoverHighFreq;
}

int CrossoverManager::getLatencySamples() const
{
    return (filterType == FilterType::FIR) ? firLatencySamples : 0;
}

} // namespace mbsc