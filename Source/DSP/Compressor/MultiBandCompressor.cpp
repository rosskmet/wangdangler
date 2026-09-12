#include "MultiBandCompressor.h"
#include <cmath>

using StyleDefaultsParams = mbsc::BandCompressorParams;

namespace mbsc {

//==============================================================================
// CompressorBand
//==============================================================================

void CompressorBand::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;

    //--- Detection & ballistics ------------------------------------
    for (auto& d : detectors)
        d.prepare(sampleRate, maxBlockSize);
    for (auto& s : smoothers)
        s.prepare(sampleRate, maxBlockSize);

    //--- Smoothed user-facing params (zipper-noise prevention) ------
    const float fsF = static_cast<float>(sampleRate);
    makeupLin    .reset(fsF, 0.02);    // 20 ms
    dryWet       .reset(fsF, 0.05);    // 50 ms
    stereoLinkAmt.reset(fsF, 0.05);

    makeupLin    .setCurrentAndTargetValue(1.0f);
    dryWet       .setCurrentAndTargetValue(1.0f);
    stereoLinkAmt.setCurrentAndTargetValue(1.0f);

    //--- Lookahead ring delay ---------------------------------------
    // Size: max lookahead + one block of headroom, rounded up to a
    // power of two so wraparound becomes a bitmask.
    const int maxDelaySamples =
        static_cast<int>(maxLookaheadMs * 0.001 * sampleRate);
    int size = 8;
    while (size < (maxDelaySamples + maxBlockSize + 1))
        size <<= 1;

    for (auto& d : delayLine)
        d.assign(static_cast<size_t>(size), 0.0f);
    delayWrite = 0;

    //--- Undelayed scratch (REAL-TIME SAFETY) -----------------------
    // Preallocated here so processBlock never touches the heap.
    unDelayedScratch.setSize(juce::jlimit(1, 2, numChannels),
                             juce::jmax(maxBlockSize, 1),
                             false, false, true);

    reset();
    prepared = true;
}

void CompressorBand::reset()
{
    for (auto& d : detectors)
        d.reset();
    for (auto& s : smoothers)
        s.reset();

    makeupLin    .setCurrentAndTargetValue(makeupLin.getCurrentValue());
    dryWet       .setCurrentAndTargetValue(dryWet.getCurrentValue());
    stereoLinkAmt.setCurrentAndTargetValue(stereoLinkAmt.getCurrentValue());

    for (auto& d : delayLine)
        std::fill(d.begin(), d.end(), 0.0f);
    delayWrite = 0;

    unDelayedScratch.clear();
    lastOutL = lastOutR = 0.0f;
    grDisplayDb.store(0.0f,  std::memory_order_relaxed);
    inLevelDb   .store(-120.0f, std::memory_order_relaxed);
    outLevelDb  .store(-120.0f, std::memory_order_relaxed);
}

void CompressorBand::setParameters(const BandCompressorParams& p)
{
    params = p;

    gainComputer.setThreshold(p.thresholdDb);
    gainComputer.setRatio(p.ratio);
    gainComputer.setKneeWidth(p.kneeDb);

    for (auto& d : detectors)
    {
        d.setDetectorType(p.detector);
        d.setRmsWindow(p.rmsWindowMs);
        d.setPeakBlend(p.peakBlend);
    }

    for (auto& s : smoothers)
    {
        s.setStyle(p.style);
        s.setAttackTime(p.attackMs);
        s.setReleaseTime(p.releaseMs);
        s.setFeedBackActive(p.topology == DetectionTopology::FeedBack);
    }

    makeupLin.setTargetValue(LevelDetector::dbToLinear(p.makeupDb));
    dryWet.setTargetValue(p.dryWetMix);
}

void CompressorBand::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (! prepared) return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jlimit(1, 2, buffer.getNumChannels());

    float* ch[2] = {
        buffer.getWritePointer(0),
        (numChannels > 1) ? buffer.getWritePointer(1) : nullptr
    };

    if (! params.enabled)
    {
        grDisplayDb.store(0.0f,   std::memory_order_relaxed);
        inLevelDb   .store(-120.0f, std::memory_order_relaxed);
        outLevelDb  .store(-120.0f, std::memory_order_relaxed);
        return;
    }

    //==================================================================
    // 0. Ensure scratch capacity (no-op in steady state — capacity
    //    was reserved in prepare(); this only guards against hosts
    //    exceeding the promised max block size)
    //==================================================================
    if (unDelayedScratch.getNumSamples()  < numSamples
        || unDelayedScratch.getNumChannels() < numChannels)
    {
        unDelayedScratch.setSize(numChannels, juce::jmax(numSamples, 1),
                                 false, false, true);
    }

    const float* det[2] = {
        unDelayedScratch.getReadPointer(0),
        (numChannels > 1) ? unDelayedScratch.getReadPointer(1) : nullptr
    };

    //==================================================================
    // 1. FUSED PASS: save undelayed copies + apply lookahead delay
    //
    //    scratch  <- input        (detection source, "sees the future")
    //    ring     <- input        (delay line write)
    //    buffer   <- ring[read]   (integer-tap delayed audio path)
    //
    // Write index advances in the OUTER sample loop; channels share
    // it — this is the correct stereo indexing pattern (the bug the
    // retired LookaheadBuffer had). Bitmask wraparound is power-of-two
    // and handles the startup-negative-index case via two's complement.
    //==================================================================
    const int delaySamples = static_cast<int>(
        uniformLookaheadMs * 0.001f * static_cast<float>(fs));
    const int delaySize    = static_cast<int>(delayLine[0].size());
    const int delayMask    = delaySize - 1;

    // Clamp: never try to read further back than the ring allows
    jassert(delaySamples < delaySize - numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = ch[c][n];

            unDelayedScratch.setSample(c, n, x);

            // Write new sample into the ring FIRST (d = 0 then reads
            // back the current sample exactly)
            delayLine[(size_t) c][(size_t) delayWrite] = x;

            // Integer read: delaySamples back from the write pointer
            const int readIdx = (delayWrite - delaySamples) & delayMask;
            ch[c][n] = delayLine[(size_t) c][(size_t) readIdx];
        }
        delayWrite = (delayWrite + 1) & delayMask;
    }

    //==================================================================
    // 2. Per-sample compression loop
    //
    //    detect (undelayed) -> stereo link (dB) -> gain computer ->
    //    ballistics -> multiply (delayed) -> wet-path makeup ->
    //    dry/wet blend
    //==================================================================
    float peakIn   = 0.0f;
    float peakOut  = 0.0f;
    float lastGrDb = 0.0f;

    float*       outL = ch[0];
    float*       outR = ch[1];
    const float* inL  = det[0];
    const float* inR  = det[1];

    for (int n = 0; n < numSamples; ++n)
    {
        peakIn = juce::jmax(peakIn, std::abs(inL[n]));

        float grDbL = 0.0f;
        float grDbR = 0.0f;

        if (params.topology == DetectionTopology::FeedForward)
        {
            //--- FF: each channel detects independently on the input --
            float levelDbL = detectors[0].processSample(inL[n]);
            float levelDbR = (inR != nullptr)
                ? detectors[1].processSample(inR[n])
                : levelDbL;

            //--- Stereo link: interpolate toward the mean, in dB ------
            if (inR != nullptr)
            {
                const float k = stereoLinkAmt.getNextValue();
                if (k > 0.001f)
                {
                    const float avg = 0.5f * (levelDbL + levelDbR);
                    levelDbL += k * (avg - levelDbL);
                    levelDbR += k * (avg - levelDbR);
                }

                grDbL = smoothers[0].processSample(
                            gainComputer.computeGain(levelDbL));
                grDbR = smoothers[1].processSample(
                            gainComputer.computeGain(levelDbR));
            }
            else
            {
                grDbL = grDbR = smoothers[0].processSample(
                            gainComputer.computeGain(levelDbL));
            }
        }
        else
        {
            //--- FB: detector chases the OUTPUT (mono-summed) ---------
            // Classic vintage topology. Mono detection mirrors the
            // hardware and sidesteps dual-loop stability problems.
            const float outMono = (inR != nullptr)
                ? 0.5f * (lastOutL + lastOutR)
                : lastOutL;

            const float fbLevelDb = detectors[0].processSample(outMono);
            grDbL = grDbR = smoothers[0].processSample(
                        gainComputer.computeGain(fbLevelDb));
        }

        //--- Apply gain to the DELAYED audio, with wet-path makeup ----
        const float mu  = makeupLin.getNextValue();
        const float mix = dryWet.getNextValue();

        {
            const float gainL = LevelDetector::dbToLinear(grDbL) * mu;
            const float xdL   = outL[n];
            const float wetL  = xdL * gainL;
            outL[n] = xdL + mix * (wetL - xdL);   // mix=1: wet, mix=0: dry

            if (inR != nullptr)
            {
                const float gainR = LevelDetector::dbToLinear(grDbR) * mu;
                const float xdR   = outR[n];
                const float wetR  = xdR * gainR;
                outR[n] = xdR + mix * (wetR - xdR);
            }
        }

        //--- Feed-back state for next sample --------------------------
        lastOutL = outL[n];
        lastOutR = (inR != nullptr) ? outR[n] : lastOutL;

        lastGrDb = grDbL;
        peakOut  = juce::jmax(peakOut, std::abs(outL[n]));
    }

    //==================================================================
    // 3. Publish metering (atomics; relaxed ordering is fine for
    //    display — worst case the GUI reads one block late)
    //==================================================================
    grDisplayDb.store(lastGrDb,                          std::memory_order_relaxed);
    inLevelDb   .store(LevelDetector::linearToDb(peakIn),  std::memory_order_relaxed);
    outLevelDb  .store(LevelDetector::linearToDb(peakOut), std::memory_order_relaxed);
}

//==============================================================================
// MultiBandCompressor
//==============================================================================

void MultiBandCompressor::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;

    for (auto& band : bands)
    {
        if (! band) band = std::make_unique<CompressorBand>();
        band->prepare(sampleRate, maxBlockSize, numChannels);
    }

    // prepare() runs on the message thread — no real-time constraint,
    // so take the REAL lock and always apply pending params. The old
    // try-lock could silently skip parameter application if it raced
    // with setBandParameters(), leaving bands on stale settings after
    // a host reconfigure (sample-rate change, buffer resize).
    {
        juce::SpinLock::ScopedLockType lock(paramLock);
        for (int b = 0; b < numBands; ++b)
        {
            bands[(size_t) b]->setParameters(pendingParams[(size_t) b]);
            paramsDirty[(size_t) b] = false;
        }
    }

    recomputeUniformLookahead();
}

void MultiBandCompressor::reset()
{
    for (auto& band : bands)
        if (band) band->reset();
}

void MultiBandCompressor::setBandParameters(int band, const BandCompressorParams& p)
{
    jassert(juce::isPositiveAndBelow(band, numBands));
    juce::SpinLock::ScopedLockType lock(paramLock);
    pendingParams[band] = p;
    requestedLookaheadMs[band] = p.lookaheadMs;
    paramsDirty[band] = true;          // ← was missing; audio thread polled a flag nothing raised
    recomputeUniformLookahead();
}

void MultiBandCompressor::setStereoLink(float amount01)
{
    const float k = juce::jlimit(0.0f, 1.0f, amount01);
    for (auto& band : bands)
        if (band) band->setStereoLinkTarget(k);
}

void MultiBandCompressor::processBand(int bandIdx, juce::AudioBuffer<float>& bandBuffer)
{
    jassert(juce::isPositiveAndBelow(bandIdx, numBands));
    auto& band = bands[bandIdx];
    if (! band) return;

    // Try-lock: if the message thread holds the lock at this instant,
    // we keep last block's params — never block the audio thread.
    {
        juce::SpinLock::ScopedTryLockType lock(paramLock);
        if (lock.isLocked() && paramsDirty[(size_t) bandIdx])
        {
            band->setParameters(pendingParams[(size_t) bandIdx]);
            paramsDirty[(size_t) bandIdx] = false;
        }
    }

    band->processBlock(bandBuffer);
}

int MultiBandCompressor::getLatencySamples() const
{
    return static_cast<int>(effectiveLookaheadMs.load() * 0.001 * fs);
}

float MultiBandCompressor::getBandGainReductionDb(int band) const
{
    return bands[band] ? bands[band]->getGainReductionDb() : 0.0f;
}

float MultiBandCompressor::getBandInputLevelDb(int band) const
{
    return bands[band] ? bands[band]->getInputLevelDb() : -120.0f;
}

float MultiBandCompressor::getBandOutputLevelDb(int band) const
{
    return bands[band] ? bands[band]->getOutputLevelDb() : -120.0f;
}

void MultiBandCompressor::recomputeUniformLookahead()
{
    float maxMs = 0.0f;
    for (float ms : requestedLookaheadMs)
        maxMs = juce::jmax(maxMs, ms);

    // Ensure bands minimum spacing so low/mid/high crossover phase stays intact
    effectiveLookaheadMs.store(maxMs);

    for (auto& band : bands)
        if (band) band->setUniformLookaheadMs(maxMs);
}

} // namespace mbsc