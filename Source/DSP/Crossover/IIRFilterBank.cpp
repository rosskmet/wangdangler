#include "IIRFilterBank.h"
#include "CrossoverManager.h"

namespace mbsc {

void IIRFilterBank::prepare (double sampleRate, int maxBlockSize)
{
    this->sampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) juce::jmax (1, maxBlockSize),
                                  (juce::uint32) maxChannels };
    lowEdge .prepare (spec);
    highEdge.prepare (spec);

    // Glides frequencies from wherever they currently sit toward target
    smoothedLowFreq .reset (sampleRate, 0.01);
    smoothedHighFreq.reset (sampleRate, 0.01);

    lowEdge .setCutoffFrequency (smoothedLowFreq .getCurrentValue());
    highEdge.setCutoffFrequency (smoothedHighFreq.getCurrentValue());
}

void IIRFilterBank::reset()
{
    lowEdge .reset();
    highEdge.reset();
}

void IIRFilterBank::setCrossoverFrequencies (float lowFreq, float highFreq)
{
    lowFreq  = juce::jlimit (MIN_CROSSOVER_FREQ, MAX_CROSSOVER_FREQ, lowFreq);
    highFreq = juce::jlimit (MIN_CROSSOVER_FREQ, MAX_CROSSOVER_FREQ, highFreq);

    if (lowFreq >= highFreq)
    {
        const float midpoint = 0.5f * (lowFreq + highFreq);
        lowFreq  = juce::jmax (MIN_CROSSOVER_FREQ, midpoint - 100.0f);
        highFreq = juce::jmin (MAX_CROSSOVER_FREQ, midpoint + 100.0f);
    }

    smoothedLowFreq .setTargetValue (lowFreq);
    smoothedHighFreq.setTargetValue (highFreq);
}

void IIRFilterBank::split (const juce::AudioBuffer<float>& input,
                           std::vector<juce::AudioBuffer<float>>& bandBuffers)
{
    if (bandBuffers.size() < 3)
    {
        jassertfalse;
        return;
    }

    // Advance the 10 ms glide and push current values to the filters
    lowEdge .setCutoffFrequency (smoothedLowFreq .getNextValue());
    highEdge.setCutoffFrequency (smoothedHighFreq.getNextValue());

    const int numSamples = input.getNumSamples();
    const int channels   = input.getNumChannels();

    for (int b = 0; b < 3; ++b)
    {
        auto& band = bandBuffers[(size_t) b];
        if (band.getNumChannels() != channels || band.getNumSamples() < numSamples)
            band.setSize (channels, numSamples, false, false, true);
    }

    auto& low  = bandBuffers[0];
    auto& mid  = bandBuffers[1];
    auto& high = bandBuffers[2];

    for (int ch = 0; ch < channels; ++ch)
    {
        // Bus layout is enforced stereo upstream; clamp protects
        // against any host oddity so state array indexes stay in range
        const int fch = juce::jlimit (0, maxChannels - 1, ch);

        const float* x  = input.getReadPointer (ch);
        float* lo = low .getWritePointer (ch);
        float* mi = mid .getWritePointer (ch);
        float* hi = high.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            float lowBand = 0.0f, upperPart = 0.0f;
            lowEdge.processSample (fch, x[n], lowBand, upperPart);

            float midBand = 0.0f, highBand = 0.0f;
            highEdge.processSample (fch, upperPart, midBand, highBand);

            lo[n] = lowBand;
            mi[n] = -midBand;   // LR summation polarity (see verification step)
            hi[n] = highBand;
        }
    }
}

void IIRFilterBank::merge (const std::vector<juce::AudioBuffer<float>>& bandBuffers,
                           juce::AudioBuffer<float>& output)
{
    output.clear();
    const int numSamples = output.getNumSamples();

    for (const auto& band : bandBuffers)
        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            if (ch < band.getNumChannels())
                output.addFrom (ch, 0, band, ch, 0, numSamples);
}

} // namespace mbsc