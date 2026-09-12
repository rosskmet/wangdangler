#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

#include "DetectionLogic.h"
#include "../Utils/SmoothedParameter.h"

namespace mbsc {

//==============================================================================
/** Per-band parameter set (owned by the audio thread via copy at block start) */
struct BandCompressorParams
{
    bool  enabled      = true;

    // Gain computer
    float thresholdDb  = -20.0f;
    float ratio        = 4.0f;
    float kneeDb       = 6.0f;

    // Ballistics
    float attackMs     = 10.0f;
    float releaseMs    = 100.0f;

    // Output shaping
    float makeupDb     = 0.0f;
    float dryWetMix    = 1.0f;      // 1.0 = fully compressed, 0.0 = bypass (no GR)

    // Character
    CompStyle          style    = CompStyle::Digital;
    DetectorType       detector = DetectorType::Peak;
    DetectionTopology  topology = DetectionTopology::FeedForward;

    // Detection tuning
    float rmsWindowMs  = 10.0f;
    float peakBlend     = 0.5f;

    // Lookahead: requested per band; MultiBandCompressor enforces uniform
    // delay across bands (see phase-coherence notes).
    float lookaheadMs  = 0.0f;
};

//==============================================================================
/**
 * Single band compressor.
 *
 * Signal flow (feed-forward, per sample n):
 *
 *   x[n] ──┬─► Detector(ch) ─► link ─► GainComputer ─► Smoother ─► g[n]
 *          │                                                        │
 *          └─► LookaheadDelay ─► x[n-D] ─►  * g[n] * G_mu ──► wet  │
 *                                   │                              │
 *                                   └──► dry ──► mix(dry, wet) ──► y[n]
 *
 * Feed-back topology redirects the detector input to the band output
 * (mono-summed), modelling vintage self-stabilising behaviour.
 */
class CompressorBand
{
public:
    CompressorBand() = default;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setParameters(const BandCompressorParams& p);

    /** Uniform lookahead applied by MultiBandCompressor (ms). */
    void setUniformLookaheadMs(float ms) { uniformLookaheadMs = ms; }

    /** Process a stereo (or mono) band buffer in place. */
    void processBlock(juce::AudioBuffer<float>& buffer);

    // Metering (thread-safe read from GUI)
    float getGainReductionDb() const { return grDisplayDb.load(std::memory_order_relaxed); }
    float getInputLevelDb()    const { return inLevelDb.load(std::memory_order_relaxed); }
    float getOutputLevelDb()   const { return outLevelDb.load(std::memory_order_relaxed); }

    /** MultiBandCompressor pushes the shared link amount in. */
    void setStereoLinkTarget(float amount01)
    {
        stereoLinkAmt.setTargetValue(juce::jlimit(0.0f, 1.0f, amount01));
    }

private:
    void processSampleFF(int numChannels, const float* in[], float* out[], int n);
    void processSampleFB(int numChannels, const float* in[], float* out[], int n);

    BandCompressorParams params;

    //--- Detection & ballistics --------------------------------------
    std::array<LevelDetector, 2>        detectors;
    std::array<GainReductionSmoother, 2> smoothers;
    GainComputer                        gainComputer;

    //--- Smoothed user-facing params -----------------------------------
    juce::SmoothedValue<float> makeupLin;
    juce::SmoothedValue<float> dryWet;
    juce::SmoothedValue<float> stereoLinkAmt;

    //--- Lookahead (uniform across bands; pushed by MultiBandCompressor)
    static constexpr float maxLookaheadMs = 20.0f;
    float uniformLookaheadMs = 0.0f;

    //--- Ring delay (power-of-two size, bitmask wraparound) --------------
    std::array<std::vector<float>, 2> delayLine;   // [channel][sample]
    int delayWrite = 0;

    //--- Preallocated undelayed scratch (real-time safety) --------------
    juce::AudioBuffer<float> unDelayedScratch;

    //--- Feed-back topology state -----------------------------------------
    float lastOutL = 0.0f;
    float lastOutR = 0.0f;

    //--- Metering ------------------------------------------------------------
    std::atomic<float> grDisplayDb { 0.0f };
    std::atomic<float> inLevelDb    { -120.0f };
    std::atomic<float> outLevelDb   { -120.0f };

    double fs = 48000.0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE(CompressorBand)
};

//==============================================================================
/**
 * MultiBandCompressor
 *
 * Owns NUM_BANDS CompressorBands and enforces:
 *  - Uniform lookahead delay across bands (crossover phase coherence)
 *  - Thread-safe parameter handoff (message thread -> audio thread)
 *  - Latency reporting for host PDC
 */
class MultiBandCompressor
{
public:
    static constexpr int numBands = 3;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Message-thread safe. */
    void setBandParameters(int band, const BandCompressorParams& p);

    /** 0 = fully independent channels, 1 = fully linked (shared across bands) */
    void setStereoLink(float amount01);

    /** Audio-thread API matching our signal chain design */
    void processBand(int band, juce::AudioBuffer<float>& bandBuffer);

    /** Overall latency incl. lookahead (samples). Report to host! */
    int getLatencySamples() const;

    // Metering accessors for the editor
    float getBandGainReductionDb(int band) const;
    float getBandInputLevelDb(int band)    const;
    float getBandOutputLevelDb(int band)   const;

    /** Effective (uniform) lookahead currently in use, in ms */
    float getEffectiveLookaheadMs() const { return effectiveLookaheadMs.load(); }

private:
    void recomputeUniformLookahead();

    std::array<BandCompressorParams, numBands> pendingParams;  // written: message thread
    std::array<bool, numBands>                 paramsDirty { true, true, true };
    juce::SpinLock paramLock;

    std::array<std::unique_ptr<CompressorBand>, numBands> bands;
    double fs = 48000.0;

    float requestedLookaheadMs[numBands] = { 0.0f, 0.0f, 0.0f };
    std::atomic<float> effectiveLookaheadMs { 0.0f };
};

} // namespace mbsc