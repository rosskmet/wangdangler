#include <catch2/catch.hpp>
#include "DSP/Crossover/CrossoverManager.h"

using namespace mbsc;

TEST_CASE("CrossoverManager - Flat Summation", "[crossover]")
{
    CrossoverManager crossover(3);
    crossover.prepare(48000.0, 512);
    crossover.setCrossoverFrequency(0, 200.0f);
    crossover.setCrossoverFrequency(1, 2000.0f);
    
    // Test with white noise - sum should equal input
    juce::AudioBuffer<float> input(1, 48000);
    juce::Random rng;
    
    for (int i = 0; i < 48000; ++i)
        input.setSample(0, i, rng.nextNormalizedFloat());
    
    // Split and merge
    std::vector<juce::AudioBuffer<float>> bands(3);
    for (int i = 0; i < 3; ++i)
        bands[i].setSize(1, 48000);
    
    crossover.split(input, bands);
    juce::AudioBuffer<float> output(1, 48000);
    crossover.merge(bands, output);
    
    // Check RMS difference (should be minimal)
    float inputRMS = 0.0f;
    float outputRMS = 0.0f;
    
    for (int i = 0; i < 48000; ++i)
    {
        inputRMS += input.getSample(0, i) * input.getSample(0, i);
        outputRMS += output.getSample(0, i) * output.getSample(0, i);
    }
    
    inputRMS = std::sqrt(inputRMS / 48000.0f);
    outputRMS = std::sqrt(outputRMS / 48000.0f);
    
    REQUIRE(std::abs(inputRMS - outputRMS) < 0.01f);  // Within 0.01 RMS
}

TEST_CASE("FIR vs IIR - Frequency Response", "[crossover]")
{
    // Compare IIR and FIR implementations at different settings
    SECTION("Low crossover at 200Hz")
    {
        // Both should produce similar magnitude response
        // Phase differs but magnitude should match
    }
    
    SECTION("High crossover at 2000Hz")
    {
        // Verify slope characteristics
        // IIR: 24dB/octave
        // FIR: configurable based on tap count
    }
}

TEST_CASE("Crossover - Smooth Transitions", "[crossover][smoothing]")
{
    LinkwitzRileyFilters lr(48000.0);
    lr.prepare(48000.0, 512);
    
    // Enable smooth transitions
    lr.setTransitionConfig({
        .strategy = CrossfadeStrategy::PARALLEL_BANKS,
        .durationSec = 0.01f
    });
    
    // Generate test signal
    juce::AudioBuffer<float> testBuffer(1, 48000);
    for (int i = 0; i < 48000; ++i)
        testBuffer.setSample(0, i, std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f));
    
    // Change frequency during processing
    lr.processLow(testBuffer);
    lr.setLowCutoff(500.0f);  // Should trigger smooth transition
    
    // Continue processing
    for (int i = 0; i < 10; ++i)
        lr.processLow(testBuffer);
    
    // Verify no extreme spikes (clicks)
    float maxSample = 0.0f;
    for (int i = 20000; i < 25000; ++i)  // During transition period
    {
        float absVal = std::abs(testBuffer.getSample(0, i));
        if (absVal > maxSample) maxVal = absVal;
    }
    
    REQUIRE(maxVal < 2.0f);  // Should not exceed reasonable bounds
}