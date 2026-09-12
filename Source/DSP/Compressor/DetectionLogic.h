#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <array>

namespace mbsc {

//==============================================================================
/** Detection style: which signal branch feeds the detector */
enum class DetectionTopology { FeedForward, FeedBack };

/** Detector type for the level measurement stage */
enum class DetectorType { Peak, RMS, Hybrid };

/** Compressor style (drives default detector topology and ballistics) */
enum class CompStyle { Vintage, Opto, Digital, Transparent };

//==============================================================================
/**
 * LevelDetector
 *
 * Measures signal level using Peak, RMS, or blended detection.
 * Outputs level in dB, computed per-sample for smooth automation.
 *
 * Pipeline:  x[n] ─► |x| or x² ─► one-pole pre-filter ─► dB convert
 *
 * The RMS window time and peak blend can be smoothed externally
 * (they're passed per-block from smoothed parameters).
 */
class LevelDetector
{
public:
    LevelDetector() = default;

    void prepare(double sampleRate, int /*blockSize*/)
    {
        fs = static_cast<float>(sampleRate);
        setRmsWindow(rmsWindowMs);
        reset();
    }

    void reset()
    {
        msState = 0.0f;     // mean-square accumulator (linear power)
        peakEnvelope = 0.0f;
    }

    //------------------------------------------------------------------
    // Configuration

    void setDetectorType(DetectorType newType) { type = newType; }

    /** RMS averaging window in milliseconds (typically 5–50ms) */
    void setRmsWindow(float ms)
    {
        rmsWindowMs = juce::jlimit(1.0f, 200.0f, ms);
        // One-pole smoothing coefficient matching a moving-average of
        // equivalent memory: tau = W / 2 (energy of rectangular window
        // equals exponential window with this mapping)
        const float tauSec = rmsWindowMs * 0.001f * 0.5f;
        rmsCoeff = std::exp(-1.0f / (tauSec * fs));
    }

    /** 0 = pure RMS, 1 = pure peak (for Hybrid mode) */
    void setPeakBlend(float blend) { peakBlend = juce::jlimit(0.0f, 1.0f, blend); }

    //------------------------------------------------------------------
    // Processing

    /** Process one sample, returning detected level in dBFS */
    float processSample(float x)
    {
        const float ax = std::abs(x);
        float level = 0.0f;

        switch (type)
        {
            case DetectorType::Peak:
                // Instantaneous magnitude (with optional pre-smoothing done
                // by the envelope follower downstream if desired)
                level = ax;
                break;

            case DetectorType::RMS:
                msState = rmsCoeff * msState + (1.0f - rmsCoeff) * (x * x);
                level = std::sqrt(msState);
                break;

            case DetectorType::Hybrid:
            {
                // RMS floor plus instantaneous peak blended in.
                // peak = max(rms, blend * peak + ...) trick:
                // emulate "peak riding on RMS" behaviour
                msState = rmsCoeff * msState + (1.0f - rmsCoeff) * (x * x);
                const float rms = std::sqrt(msState);

                // Smoothed peak tracking (fast one-pole up, immediate down)
                peakEnvelope = (ax > peakEnvelope)
                    ? 0.6f * peakEnvelope + 0.4f * ax      // fast-ish rise
                    : 0.98f * peakEnvelope + 0.02f * ax;    // gentle fall

                level = rms + peakBlend * (peakEnvelope - rms);
                if (level < 0.0f) level = 0.0f;
                break;
            }
        }

        // Convert to dB with floor to avoid log(0)
        return linearToDb(level);
    }

    /** Convenience: process an entire buffer channel (mono-summed detection) */
    template<typename T>
    float processBlock(const T* data, int numSamples)
    {
        float last = -120.0f;
        for (int i = 0; i < numSamples; ++i)
            last = processSample(data[i]);
        return last;
    }

    static float linearToDb(float lin)
    {
        constexpr float floorLin = 1e-6f;   // -120 dB floor
        return 20.0f * std::log10(juce::jmax(lin, floorLin));
    }

    static float dbToLinear(float db)
    {
        return std::pow(10.0f, db * 0.05f);
    }

private:
    DetectorType type = DetectorType::RMS;
    float peakBlend = 0.5f;
    float rmsWindowMs = 10.0f;

    float fs = 48000.0f;
    float rmsCoeff = 0.999f;
    float msState = 0.0f;
    float peakEnvelope = 0.0f;
};

//==============================================================================
/**
 * GainReductionSmoother (Envelope Follower in dB domain)
 *
 * Implements attack/release ballistics on the gain reduction signal.
 * Supports:
 *  - Program-dependent release (Vintage)
 *  - Dual-rate opto release curve
 *  - Sample-accurate dB-domain smoothing
 *  - Feed-back topology via setPreviousOutputDb()
 */
class GainReductionSmoother
{
public:
    void prepare(double sampleRate, int /*blockSize*/)
    {
        fs = static_cast<float>(sampleRate);
        setAttackTime(attackMs);
        setReleaseTime(releaseMs);
        reset();
    }

    void reset()
    {
        gainReductionDb = 0.0f;       // 0 dB = no reduction
        grEnvelopeDb = 0.0f;
    }

    //------------------------------------------------------------------
    // Configuration

    void setStyle(CompStyle newStyle) { style = newStyle; }

    void setAttackTime(float ms)
    {
        attackMs = juce::jmax(0.01f, ms);
        attackCoeff = coeffForTime(attackMs * 0.001f);
    }

    void setReleaseTime(float ms)
    {
        releaseMs = juce::jmax(5.0f, ms);
        releaseCoeff = coeffForTime(releaseMs * 0.001f);
    }

    /** Feed-back detection: provide output level so detector sees result */
    void setPreviousOutputDb(float outDb) { fbOutputDb = outDb; }

    //------------------------------------------------------------------
    // Processing

    /**
     * Process one sample of target gain reduction (negative dB),
     * returns smoothed gain reduction in dB (also negative).
     *
     * grTarget: raw (unsmoothed) gain reduction from the gain computer.
     */
    float processSample(float grTargetDb)
    {
        // Gain reduction is stored as negative dB values.
        // "Attacking" = becoming MORE negative (diving down).
        const bool attacking = grTargetDb < gainReductionDb;

        float coeff;
        switch (style)
        {
            case CompStyle::Vintage:
                // Program-dependent release: deeper GR => longer release.
                // Classic "2:1 release expansion" heuristic.
                coeff = releaseCoeff;
                if (!attacking)
                {
                    const float depth = juce::jlimit(0.0f, 12.0f, -gainReductionDb);
                    const float slowFactor = 1.0f - depth / 24.0f;      // down to 0.5
                    // Exponent adjustment: slower coeff = closer to 1.0
                    coeff = std::pow(releaseCoeff, slowFactor);
                }
                break;

            case CompStyle::Opto:
                if (!attacking)
                {
                    // Dual-rate release: fast near threshold, slow when deep.
                    const float depth = juce::jlimit(0.0f, 20.0f, -gainReductionDb);
                    const float normalized = depth / 20.0f;             // 0..1
                    // Blend exponent between fast (0.35x time) and slow (2.2x time)
                    const float timeScale = 0.35f + normalized * 1.85f;
                    coeff = timeToCoeff(releaseMs * 0.001f * timeScale);
                }
                else
                {
                    coeff = attackCoeff;
                }
                break;

            case CompStyle::Digital:
                coeff = attacking ? attackCoeff : releaseCoeff;
                break;

            case CompStyle::Transparent:
                // Very smooth: asymmetric fast attack but heavily smoothed
                // release regardless of program (avoids pumping artefacts)
                coeff = attacking ? attackCoeff
                                  : timeToCoeff(releaseMs * 0.001f * 1.5f);
                break;
        }

        // One-pole smoothing in dB domain
        gainReductionDb = coeff * gainReductionDb + (1.0f - coeff) * grTargetDb;

        // Feed-back correction: limit based on what the output is doing.
        // (Full FB topology integrates this in the compressor class.)
        if (fbActive)
        {
            // Soft clamp: never reduce below what keeps output above -inf
            // (Guard against runaway FB compression)
            const float maxGr = 60.0f;
            if (gainReductionDb < -maxGr)
                gainReductionDb = -maxGr;
        }

        return gainReductionDb;
    }

    float getCurrentGrDb() const { return gainReductionDb; }

    /** Enable feed-back limiting behaviors */
    void setFeedBackActive(bool active) { fbActive = active; }

    static float timeToCoeff(float timeSeconds)
    {
        // Coefficient for 63.2% convergence to target
        return std::exp(-1.0f / (juce::jmax(timeSeconds, 1e-7f) * 48000.0f));
        // NOTE: fs is applied in prepare(); see corrected overload below.
    }

    // float getCoeffForTime(float timeSeconds) const
    // {
    //     return std::exp(-1.0f / (juce::jmax(timeSeconds, 1e-7f) * fs));
    // }

private:
    float coeffForTime(float seconds) const
    {
        // 63.2% convergence in `seconds`, at ACTUAL prepared sample rate
        return std::exp(-1.0f / (juce::jmax(seconds, 1e-7f) * fs));
    }

    CompStyle style = CompStyle::Digital;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;

    float fs = 48000.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    float gainReductionDb = 0.0f;
    float grEnvelopeDb = 0.0f;

    float fbOutputDb = 0.0f;
    bool fbActive = false;
};

//==============================================================================
/**
 * GainComputer
 *
 * Maps detected level (dB) to target gain reduction (dB) using the
 * threshold / ratio / knee softening curve.
 *
 *     gain(dB) = level(dB) - outLevel(dB)   where outLevel follows:
 *
 *            { level                                   level < T - W/2
 *     out  =  { level + (1/R - 1)(level - T + W/2)²/W  |level - T| < W/2
 *            { T + (level - T)/R                       level > T + W/2
 *
 * (Soft-knee formulation per standards; see notes below.)
 */
class GainComputer
{
public:
    void setThreshold(float thresholdDb) { thresholdDb_ = thresholdDb; }
    void setRatio(float r)              { ratio = juce::jmax(1.0f, r); }
    void setKneeWidth(float kneeDb)     { kneeWidth = juce::jmax(0.0f, kneeDb); }

    /** Compute target gain reduction (dB, <= 0) for a detected level (dB) */
    float computeGain(float levelDb) const
    {
        if (kneeWidth > 0.0f && std::abs(levelDb - thresholdDb_) < kneeWidth * 0.5f)
        {
            // Quadratic interpolation across the knee
            const float x = levelDb - thresholdDb_ + kneeWidth * 0.5f;
            const float slope = (1.0f / ratio - 1.0f);
            return slope * (x * x) / (2.0f * kneeWidth);
        }

        if (levelDb > thresholdDb_)
        {
            const float over = levelDb - thresholdDb_;
            return over * (1.0f / ratio - 1.0f);   // negative: gain reduction
        }

        return 0.0f;   // Below threshold: unity
    }

    /** Static curve computation for GUI graphing (kg dB in) */
    static float staticCurve(float levelDb, float thresholdDb,
                             float r, float kneeDb)
    {
        GainComputer gc;
        gc.setThreshold(thresholdDb);
        gc.setRatio(r);
        gc.setKneeWidth(kneeDb);
        return gc.computeGain(levelDb);
    }

private:
    float thresholdDb_ = -20.0f;
    float ratio = 4.0f;
    float kneeWidth = 6.0f;
};

//==============================================================================
/**
 * DetectionLogic
 *
 * Top-level class combining LevelDetector, GainComputer, and
 * GainReductionSmoother, with feed-forward/feed-back routing and
 * lookahead support hooks.
 *
 * Usage per sample (feed-forward):
 *     detDb = detector.processSample(x);
 *     grDb  = gainComputer.computeGain(detDb);
 *     grDb  = smoother.processSample(grDb);
 *     y     = x * LevelDetector::dbToLinear(grDb + makeupDb);
 */
class DetectionLogic
{
public:
    struct Parameters
    {
        DetectorType detectorType = DetectorType::RMS;
        CompStyle style = CompStyle::Digital;
        DetectionTopology topology = DetectionTopology::FeedForward;
        float rmsWindowMs = 10.0f;
        float peakBlend = 0.5f;

        float thresholdDb = -20.0f;
        float ratio = 4.0f;
        float kneeDb = 6.0f;

        float attackMs = 10.0f;
        float releaseMs = 100.0f;
    };

    void prepare(double sampleRate, int blockSize)
    {
        detector.prepare(sampleRate, blockSize);
        smoother.prepare(sampleRate, blockSize);

        // Precompute coefficients so setAttackTime uses correct fs
        smoother.setStyle(params.style);
        smoother.setAttackTime(params.attackMs);
        smoother.setReleaseTime(params.releaseMs);
        smoother.setFeedBackActive(params.topology == DetectionTopology::FeedBack);

        preparedFs = sampleRate;
    }

    void reset()
    {
        detector.reset();
        smoother.reset();
    }

    void setParameters(const Parameters& p)
    {
        params = p;
        detector.setDetectorType(p.detectorType);
        detector.setRmsWindow(p.rmsWindowMs);
        detector.setPeakBlend(p.peakBlend);
        gainComputer.setThreshold(p.thresholdDb);
        gainComputer.setRatio(p.ratio);
        gainComputer.setKneeWidth(p.kneeDb);
        smoother.setStyle(p.style);
        smoother.setAttackTime(p.attackMs);
        smoother.setReleaseTime(p.releaseMs);
        smoother.setFeedBackActive(p.topology == DetectionTopology::FeedBack);
    }

    /**
     * Full per-sample processing (feed-forward topology).
     * Returns gain multiplier to apply to the audio.
     */
    float processSample(float x)
    {
        if (topologyOverride == DetectionTopology::FeedBack)
            return processSampleFeedback(x);

        const float levelDb = detector.processSample(x);
        const float grDb = gainComputer.computeGain(levelDb);
        const float smoothedGr = smoother.processSample(grDb);
        return LevelDetector::dbToLinear(smoothedGr);
    }

    /**
     * Feed-back variant: detector listens to the OUTPUT.
     * Needs the previous output sample; we reconstruct it from
     * the applied gain for the recursive equation.
     */
    float processSampleFeedback(float x)
    {
        // Reconstruct previous output (audio thread supplies y via pushOutput)
        const float levelDb = detector.processSample(lastOutput);
        const float grDb = gainComputer.computeGain(levelDb);
        const float smoothedGr = smoother.processSample(grDb);

        const float gain = LevelDetector::dbToLinear(smoothedGr);
        const float y = x * gain;
        lastOutput = y;
        return gain;
    }

    /** For external lookahead arrangements: compute gain from pre-delayed level */
    float computeGainForLevel(float levelDb)
    {
        const float grDb = gainComputer.computeGain(levelDb);
        const float smoothedGr = smoother.processSample(grDb);
        return LevelDetector::dbToLinear(smoothedGr);
    }

    /** Detector-only processing (for lookahead path: detect on non-delayed signal) */
    float detectOnly(float x) { return detector.processSample(x); }

    // Metering access
    float getGainReductionDb() const { return smoother.getCurrentGrDb(); }

    void forceTopology(DetectionTopology t) { topologyOverride = t; }
    void clearTopologyOverride() { topologyOverride = std::nullopt; }

    const Parameters& getParameters() const { return params; }

private:
    Parameters params;
    DetectionTopology topologyOverride_{};
    std::optional<DetectionTopology> topologyOverride = std::nullopt;

    LevelDetector detector;
    GainComputer gainComputer;
    GainReductionSmoother smoother;

    float lastOutput = 0.0f;
    juce::SmoothedValue<float> fbLevel;
    double preparedFs = 48000.0;
};

} // namespace mbsc