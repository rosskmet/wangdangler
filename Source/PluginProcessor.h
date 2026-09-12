#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/Crossover/CrossoverManager.h"
#include "DSP/Compressor/MultiBandCompressor.h"
#include "DSP/Saturation/MultiBandSaturator.h"
#include "Parameters/ParameterManager.h"
#include "Parameters/ParameterIDs.h"

namespace mbsc
{

class WangdanglerAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    //==============================================================================
    WangdanglerAudioProcessor();
    ~WangdanglerAudioProcessor() override;

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Silence the benign "hides AudioProcessor::processBlock(double)" warning
    using AudioProcessor::processBlock;

    //==============================================================================
    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread parameter pump — pairs with the audio thread's try-lock.
    // startTimerHz(30) is called in the constructor.
    void timerCallback() override;

    ParameterManager& getParameterManager() { return paramManager; }

private:
    //==============================================================================
    void updateLatency();
    void pullParametersToDSP();

    //==============================================================================
    std::unique_ptr<CrossoverManager>  crossover;
    std::unique_ptr<MultiBandCompressor> compressor;
    std::unique_ptr<MultiBandSaturator>  saturator;

    std::vector<juce::AudioBuffer<float>> bandBuffers;

    juce::SmoothedValue<float> inputGainLin;
    juce::SmoothedValue<float> outputGainLin;

    ParameterManager paramManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WangdanglerAudioProcessor)
};

} // namespace mbsc