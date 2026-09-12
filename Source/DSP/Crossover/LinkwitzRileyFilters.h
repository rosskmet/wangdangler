#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace mbsc {

/**
 * CrossfadeStrategy for smooth transitions
 * - NONE: Abrupt change (not recommended)
 * - PER_SAMPLE: Smooth coefficient updates each sample
 * - PARALLEL_BANKS: Two filter banks crossfaded over time
 * - PHASE_PRESERVING: Maintains phase coherence during transition
 */
enum class CrossfadeStrategy {
    NONE,
    PER_SAMPLE,
    PARALLEL_BANKS,
    PHASE_PRESERVING
};

struct FilterTransitionConfig {
    CrossfadeStrategy strategy = CrossfadeStrategy::PARALLEL_BANKS;
    float durationSec = 0.01f;      // 10ms transition by default
    int maxSamples = 480;           // At 48kHz, this = 10ms
    bool preservePhase = true;
};

class LinkwitzRileyFilters
{
public:
    explicit LinkwitzRileyFilters(double sampleRate);
    ~LinkwitzRileyFilters();

    void prepare(double sampleRate, int blockSize);
    void reset();
    
    void setLowCutoff(float freqHz);
    void setHighCutoff(float freqHz);
    
    // Configure transition behavior
    void setTransitionConfig(const FilterTransitionConfig& config);
    FilterTransitionConfig getTransitionConfig() const;
    
    // Process with smooth transitions
    void processLow(juce::AudioBuffer<float>& buffer);
    void processMid(juce::AudioBuffer<float>& buffer);
    void processHigh(juce::AudioBuffer<float>& buffer);
    
    // Per-sample processing (for real-time)
    float processLowSample(float sample);
    float processMidSample(float sample);
    float processHighSample(float sample);
    
    // Get current coefficients (for visualization)
    // LinkwitzRileyFilters.h — replace the three Ptr declarations:
    std::array<float, 5> getCurrentLowCoeffs()  const { return currentLowCoeffs; }
    std::array<float, 5> getCurrentMidCoeffs()  const { return currentMidCoeffs; }
    std::array<float, 5> getCurrentHighCoeffs() const { return currentHighCoeffs; }
    
    // Debug: Check if transition is active
    bool isTransitioning() const;
    float getTransitionProgress() const;  // 0.0 to 1.0
    
    int getLatencySamples() const { return 0; }

private:
    void updateCoefficients();
    void scheduleCoefficientUpdate();
    void performCoefficientInterpolation();
    void resetFilterState();
    
    // Parallel bank method
    void initParallelBanks();
    void cleanupParallelBanks();
    void crossfadeBetweenBanks(juce::AudioBuffer<float>& buffer, int channel);
    
    // Coefficient smoothing (per-sample)
    void smoothCoefficients(float targetLowFreq, float targetHighFreq);
    float lerpCoeff(float current, float target, float t);
    
    // State preservation
    void captureCurrentState();
    void restorePreviousState();
    
    double sampleRate = 48000.0;
    float lowCutoff = 200.0f;
    float highCutoff = 2000.0f;
    
    // Target frequencies (for interpolation)
    float targetLowCutoff = 200.0f;
    float targetHighCutoff = 2000.0f;
    
    // Transition state
    FilterTransitionConfig transitionConfig;
    bool transitioning = false;
    int transitionCounter = 0;
    int transitionSamplesRemaining = 0;
    float transitionAlpha = 0.0f;  // Crossfade factor
    
    // Previous coefficients (for interpolation)
    std::array<float, 5> prevLowCoeffs{};   // b0, b1, b2, a1, a2
    std::array<float, 5> prevMidCoeffs{};
    std::array<float, 5> prevHighCoeffs{};
    
    std::array<float, 5> currentLowCoeffs{};
    std::array<float, 5> currentMidCoeffs{};
    std::array<float, 5> currentHighCoeffs{};
    
    // Actual filter objects
    struct BiquadFilter {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;  // State variables
        
        void reset() { z1 = z2 = 0.0f; }
        float process(float sample) {
            float result = b0 * sample + z1;
            z1 = b1 * sample - a1 * result + z2;
            z2 = b2 * sample - a2 * result;
            return result;
        }
    };
    
    // Low band filters (2 cascaded biquads)
    BiquadFilter lpLow1_a, lpLow1_b;
    BiquadFilter lpLow2_a, lpLow2_b;
    
    // Mid band filters (HP + LP = bandpass)
    BiquadFilter hpMid1_a, hpMid1_b;
    BiquadFilter hpMid2_a, hpMid2_b;
    BiquadFilter lpMid1_a, lpMid1_b;
    BiquadFilter lpMid2_a, lpMid2_b;
    
    // High band filters (2 cascaded biquads)
    BiquadFilter hpHigh1_a, hpHigh1_b;
    BiquadFilter hpHigh2_a, hpHigh2_b;
    
    // Parallel bank storage (for crossfade strategy)
    struct ParallelBank {
        BiquadFilter lpLow1_a, lpLow1_b;
        BiquadFilter lpLow2_a, lpLow2_b;
        BiquadFilter hpMid1_a, hpMid1_b;
        BiquadFilter hpMid2_a, hpMid2_b;
        BiquadFilter lpMid1_a, lpMid1_b;
        BiquadFilter lpMid2_a, lpMid2_b;
        BiquadFilter hpHigh1_a, hpHigh1_b;
        BiquadFilter hpHigh2_a, hpHigh2_b;
        
        void resetAll() {
            lpLow1_a.reset(); lpLow1_b.reset();
            lpLow2_a.reset(); lpLow2_b.reset();
            hpMid1_a.reset(); hpMid1_b.reset();
            hpMid2_a.reset(); hpMid2_b.reset();
            lpMid1_a.reset(); lpMid1_b.reset();
            lpMid2_a.reset(); lpMid2_b.reset();
            hpHigh1_a.reset(); hpHigh1_b.reset();
            hpHigh2_a.reset(); hpHigh2_b.reset();
        }
    };
    
    ParallelBank oldBank;
    ParallelBank newBank;
    bool useParallelBanks = false;
};

} // namespace mbsc