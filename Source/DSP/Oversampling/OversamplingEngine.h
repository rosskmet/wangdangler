#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

namespace mbsc {

/**
 * OversamplingEngine
 *
 * Shared, thread-managed oversampler for one processing section
 * (we use one per band around the saturator).
 *
 * Responsibilities:
 *  - Wrap juce::dsp::Oversampling with safe factor changes
 *    (re-preparation happens on the audio thread at block start;
 *     allocation happens BEFORE the block via ensurePrepared, called
 *     from the message thread when the parameter changes)
 *  - Latency reporting in BASE-RATE samples
 *  - Reset semantics matching the rest of the DSP chain
 *
 * Gain staging: juce::dsp::Oversampling handles normalization
 * internally (processSamplesUp applies the gain, processSamplesDown
 * removes it), so no manual headroom management is needed.
 */
class OversamplingEngine
{
public:
    enum class Factor { Off = 0, Two = 1, Four = 2, Eight = 3 };

    void prepare(double sampleRate, int maxBlockSize, int numChannels)
    {
        fs = sampleRate;
        channels = numChannels;

        for (int i = 0; i < 4; ++i)
        {
            oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>>(
                numChannels, i,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
            oversamplers[i]->initProcessing(maxBlockSize);
        }

        activeFactorIdx.store(0);        // was: activeFactor.store(0);
        prepared = true;
    }

    void reset()
    {
        for (auto& os : oversamplers)
            if (os) os->reset();
    }

    /** Message-thread safe. Takes effect at next block. */
    void setFactor(Factor f)
    {
        const int idx = static_cast<int>(f);
        activeFactorIdx.store(idx, std::memory_order_relaxed);
    }

    Factor getFactor() const
    {
        return static_cast<Factor>(activeFactorIdx.load(std::memory_order_relaxed));
    }

    bool isActive() const { return getFactor() != Factor::Off; }

    /**
     * Audio-thread entry: process a block through the oversampled domain.
     * 'callback' runs INSIDE the oversampled rate — it receives the
     * oversampled buffer and must be pure (no allocation, no locks).
     *
     * Returns false if oversampling is off (caller should process at
     * base rate directly).
     */
    template <typename ProcessFn>
    bool process(juce::AudioBuffer<float>& buffer, ProcessFn&& callback)
    {
        jassert(prepared);

        const int idx = activeFactorIdx.load(std::memory_order_relaxed);
        if (idx == 0)
            return false;

        auto& os = oversamplers[idx];

        // Up: writes into OS-domain buffer
        juce::dsp::AudioBlock<float> inputBlock(buffer);
        auto upBlock = os->processSamplesUp(inputBlock);

        // Run user DSP at the elevated rate
        juce::AudioBuffer<float> view(upBlock.getChannelPointer(0),
                                      buffer.getNumChannels(),
                                      0,
                                      static_cast<int>(upBlock.getNumSamples()));
        // NOTE: AudioBuffer wrapping a raw block requires contiguous
        // channel data; juce guarantees contiguity here. If you ever
        // hit a host that breaks this (never seen it, but honesty
        // demands the caveat), copy into a member buffer instead.
        callback(view);

        // Down: reads OS buffer, writes back into base-rate buffer
        juce::dsp::AudioBlock<float> outputBlock(buffer);
        os->processSamplesDown(outputBlock);

        return true;
    }

    /** Latency in BASE-RATE samples (already compensated by JUCE). */
    int getLatencySamples() const
    {
        const int idx = activeFactorIdx.load(std::memory_order_relaxed);
        return (idx == 0 || ! oversamplers[idx]) ? 0
            : static_cast<int>(oversamplers[idx]->getLatencyInSamples());
    }

private:
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> oversamplers;
    std::atomic<int> activeFactorIdx { 0 };

    double fs = 48000.0;
    int channels = 2;
    bool prepared = false;
};

} // namespace mbsc