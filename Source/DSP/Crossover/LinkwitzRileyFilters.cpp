#include "LinkwitzRileyFilters.h"
#include <cmath>

namespace mbsc {

LinkwitzRileyFilters::LinkwitzRileyFilters(double sampleRate)
    : sampleRate(sampleRate)
{
}

LinkwitzRileyFilters::~LinkwitzRileyFilters() = default;

void LinkwitzRileyFilters::prepare(double sampleRate, int blockSize)
{
    this->sampleRate = sampleRate;
    reset();
    updateCoefficients();
}

void LinkwitzRileyFilters::reset()
{
    // Reset all filter states
    lpLow1_a.reset(); lpLow1_b.reset();
    lpLow2_a.reset(); lpLow2_b.reset();
    hpMid1_a.reset(); hpMid1_b.reset();
    hpMid2_a.reset(); hpMid2_b.reset();
    lpMid1_a.reset(); lpMid1_b.reset();
    lpMid2_a.reset(); lpMid2_b.reset();
    hpHigh1_a.reset(); hpHigh1_b.reset();
    hpHigh2_a.reset(); hpHigh2_b.reset();
    
    oldBank.resetAll();
    newBank.resetAll();
    
    transitioning = false;
    transitionCounter = 0;
    transitionSamplesRemaining = 0;
    transitionAlpha = 0.0f;
}

void LinkwitzRileyFilters::setTransitionConfig(const FilterTransitionConfig& config)
{
    transitionConfig = config;
    useParallelBanks = (config.strategy == CrossfadeStrategy::PARALLEL_BANKS ||
                        config.strategy == CrossfadeStrategy::PHASE_PRESERVING);
}

FilterTransitionConfig LinkwitzRileyFilters::getTransitionConfig() const
{
    return transitionConfig;
}

// LinkwitzRileyFilters.cpp

void LinkwitzRileyFilters::setLowCutoff(float freqHz)
{
    if (freqHz < 20.0f)       freqHz = 20.0f;
    if (freqHz >= highCutoff) freqHz = highCutoff - 10.0f;

    if (std::abs(freqHz - lowCutoff) > 1.0f)
    {
        lowCutoff = freqHz;
        updateCoefficients();
        // Deliberately do NOT call resetFilterState() —
        // preserving z1/z2 across coefficient changes is what makes
        // small per-block updates click-free.
    }
    else
    {
        lowCutoff = freqHz;
    }
}

void LinkwitzRileyFilters::setHighCutoff(float freqHz)
{
    if (freqHz <= lowCutoff) freqHz = lowCutoff + 10.0f;
    if (freqHz > 20000.0f)   freqHz = 20000.0f;

    if (std::abs(freqHz - highCutoff) > 1.0f)
    {
        highCutoff = freqHz;
        updateCoefficients();
    }
    else
    {
        highCutoff = freqHz;
    }
}

void LinkwitzRileyFilters::captureCurrentState()
{
    // Capture current filter state into oldBank
    oldBank.lpLow1_a = lpLow1_a;
    oldBank.lpLow1_b = lpLow1_b;
    oldBank.lpLow2_a = lpLow2_a;
    oldBank.lpLow2_b = lpLow2_b;
    oldBank.hpMid1_a = hpMid1_a;
    oldBank.hpMid1_b = hpMid1_b;
    oldBank.hpMid2_a = hpMid2_a;
    oldBank.hpMid2_b = hpMid2_b;
    oldBank.lpMid1_a = lpMid1_a;
    oldBank.lpMid1_b = lpMid1_b;
    oldBank.lpMid2_a = lpMid2_a;
    oldBank.lpMid2_b = lpMid2_b;
    oldBank.hpHigh1_a = hpHigh1_a;
    oldBank.hpHigh1_b = hpHigh1_b;
    oldBank.hpHigh2_a = hpHigh2_a;
    oldBank.hpHigh2_b = hpHigh2_b;
}

void LinkwitzRileyFilters::scheduleCoefficientUpdate()
{
    // Compute new coefficients based on target frequencies
    auto computeBiquadCoeffs = [this](float freq, bool highPass) -> std::array<float, 5>
    {
        std::array<float, 5> coeffs{};
        float w0 = 2.0f * juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
        float cosW0 = std::cos(w0);
        float sinW0 = std::sin(w0);
        float alpha = sinW0 / (2.0f * std::sqrt(2.0f));  // Q = 1/(2*sqrt(2)) for LR
        
        if (highPass)
        {
            // High-pass biquad
            float b0 = (1.0f + cosW0) / (2.0f * (1.0f + alpha));
            float b1 = -(1.0f + cosW0) / (1.0f + alpha);
            float b2 = (1.0f + cosW0) / (2.0f * (1.0f + alpha));
            float a1 = -2.0f * cosW0 / (1.0f + alpha);
            float a2 = (1.0f - alpha) / (1.0f + alpha);
            coeffs = {b0, b1, b2, a1, a2};
        }
        else
        {
            // Low-pass biquad
            float b0 = (1.0f - cosW0) / (2.0f * (1.0f + alpha));
            float b1 = (1.0f - cosW0) / (1.0f + alpha);
            float b2 = (1.0f - cosW0) / (2.0f * (1.0f + alpha));
            float a1 = -2.0f * cosW0 / (1.0f + alpha);
            float a2 = (1.0f - alpha) / (1.0f + alpha);
            coeffs = {b0, b1, b2, a1, a2};
        }
        
        return coeffs;
    };
    
    // Store new coefficients
    auto lpLow = computeBiquadCoeffs(lowCutoff, false);
    auto hpMid = computeBiquadCoeffs(lowCutoff, true);
    auto lpMid = computeBiquadCoeffs(highCutoff, false);
    auto hpHigh = computeBiquadCoeffs(highCutoff, true);
    
    // Apply to newBank (first stage of each cascade)
    newBank.lpLow1_a.b0 = lpLow[0]; newBank.lpLow1_a.b1 = lpLow[1]; newBank.lpLow1_a.b2 = lpLow[2];
    newBank.lpLow1_a.a1 = lpLow[3]; newBank.lpLow1_a.a2 = lpLow[4];
    
    newBank.lpLow2_a.b0 = lpLow[0]; newBank.lpLow2_a.b1 = lpLow[1]; newBank.lpLow2_a.b2 = lpLow[2];
    newBank.lpLow2_a.a1 = lpLow[3]; newBank.lpLow2_a.a2 = lpLow[4];
    
    newBank.hpMid1_a.b0 = hpMid[0]; newBank.hpMid1_a.b1 = hpMid[1]; newBank.hpMid1_a.b2 = hpMid[2];
    newBank.hpMid1_a.a1 = hpMid[3]; newBank.hpMid1_a.a2 = hpMid[4];
    
    newBank.hpMid2_a.b0 = hpMid[0]; newBank.hpMid2_a.b1 = hpMid[1]; newBank.hpMid2_a.b2 = hpMid[2];
    newBank.hpMid2_a.a1 = hpMid[3]; newBank.hpMid2_a.a2 = hpMid[4];
    
    newBank.lpMid1_a.b0 = lpMid[0]; newBank.lpMid1_a.b1 = lpMid[1]; newBank.lpMid1_a.b2 = lpMid[2];
    newBank.lpMid1_a.a1 = lpMid[3]; newBank.lpMid1_a.a2 = lpMid[4];
    
    newBank.lpMid2_a.b0 = lpMid[0]; newBank.lpMid2_a.b1 = lpMid[1]; newBank.lpMid2_a.b2 = lpMid[2];
    newBank.lpMid2_a.a1 = lpMid[3]; newBank.lpMid2_a.a2 = lpMid[4];
    
    newBank.hpHigh1_a.b0 = hpHigh[0]; newBank.hpHigh1_a.b1 = hpHigh[1]; newBank.hpHigh1_a.b2 = hpHigh[2];
    newBank.hpHigh1_a.a1 = hpHigh[3]; newBank.hpHigh1_a.a2 = hpHigh[4];
    
    newBank.hpHigh2_a.b0 = hpHigh[0]; newBank.hpHigh2_a.b1 = hpHigh[1]; newBank.hpHigh2_a.b2 = hpHigh[2];
    newBank.hpHigh2_a.a1 = hpHigh[3]; newBank.hpHigh2_a.a2 = hpHigh[4];
}

void LinkwitzRileyFilters::updateCoefficients()
{
    // Direct coefficient computation (no interpolation)
    auto computeBiquadCoeffs = [this](float freq, bool highPass) -> std::array<float, 5>
    {
        std::array<float, 5> coeffs{};
        float w0 = 2.0f * juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
        float cosW0 = std::cos(w0);
        float sinW0 = std::sin(w0);
        float alpha = sinW0 / (2.0f * std::sqrt(2.0f));
        
        if (highPass)
        {
            coeffs[0] = (1.0f + cosW0) / (2.0f * (1.0f + alpha));
            coeffs[1] = -(1.0f + cosW0) / (1.0f + alpha);
            coeffs[2] = (1.0f + cosW0) / (2.0f * (1.0f + alpha));
            coeffs[3] = -2.0f * cosW0 / (1.0f + alpha);
            coeffs[4] = (1.0f - alpha) / (1.0f + alpha);
        }
        else
        {
            coeffs[0] = (1.0f - cosW0) / (2.0f * (1.0f + alpha));
            coeffs[1] = (1.0f - cosW0) / (1.0f + alpha);
            coeffs[2] = (1.0f - cosW0) / (2.0f * (1.0f + alpha));
            coeffs[3] = -2.0f * cosW0 / (1.0f + alpha);
            coeffs[4] = (1.0f - alpha) / (1.0f + alpha);
        }
        
        return coeffs;
    };
    
    auto lpLow = computeBiquadCoeffs(lowCutoff, false);
    auto hpMid = computeBiquadCoeffs(lowCutoff, true);
    auto lpMid = computeBiquadCoeffs(highCutoff, false);
    auto hpHigh = computeBiquadCoeffs(highCutoff, true);
    
    // Update current coefficients
    currentLowCoeffs = lpLow;
    currentMidCoeffs = hpMid;  // Simplified for visualization
    currentHighCoeffs = hpHigh;
    
    // Apply to all biquad stages
    for (int stage = 0; stage < 2; ++stage)
    {
        auto& lpa = (stage == 0) ? lpLow1_a : lpLow2_a;
        auto& lpb = (stage == 0) ? lpLow1_b : lpLow2_b;
        lpa.b0 = lpLow[0]; lpa.b1 = lpLow[1]; lpa.b2 = lpLow[2];
        lpa.a1 = lpLow[3]; lpa.a2 = lpLow[4];
        lpb = lpa;
        
        auto& hpa = (stage == 0) ? hpMid1_a : hpMid2_a;
        auto& hpb = (stage == 0) ? hpMid1_b : hpMid2_b;
        hpa.b0 = hpMid[0]; hpa.b1 = hpMid[1]; hpa.b2 = hpMid[2];
        hpa.a1 = hpMid[3]; hpa.a2 = hpMid[4];
        hpb = hpa;
        
        auto& lpa_mid = (stage == 0) ? lpMid1_a : lpMid2_a;
        auto& lpb_mid = (stage == 0) ? lpMid1_b : lpMid2_b;
        lpa_mid.b0 = lpMid[0]; lpa_mid.b1 = lpMid[1]; lpa_mid.b2 = lpMid[2];
        lpa_mid.a1 = lpMid[3]; lpa_mid.a2 = lpMid[4];
        lpb_mid = lpa_mid;
        
        auto& hpa_high = (stage == 0) ? hpHigh1_a : hpHigh2_a;
        auto& hpb_high = (stage == 0) ? hpHigh1_b : hpHigh2_b;
        hpa_high.b0 = hpHigh[0]; hpa_high.b1 = hpHigh[1]; hpa_high.b2 = hpHigh[2];
        hpa_high.a1 = hpHigh[3]; hpa_high.a2 = hpHigh[4];
        hpb_high = hpa_high;
    }
}

void LinkwitzRileyFilters::processLow(juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        if (!transitioning)
        {
            // Standard processing
            auto* data = buffer.getWritePointer(channel);
            const int numSamples = buffer.getNumSamples();
            
            for (int sample = 0; sample < numSamples; ++sample)
            {
                float s = data[sample];
                s = lpLow1_a.process(s);
                s = lpLow1_b.process(s);
                s = lpLow2_a.process(s);
                s = lpLow2_b.process(s);
                data[sample] = s;
            }
        }
        else
        {
            // Crossfade processing
            crossfadeBetweenBanks(buffer, channel);
        }
    }
}

void LinkwitzRileyFilters::processMid(juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        if (!transitioning)
        {
            auto* data = buffer.getWritePointer(channel);
            const int numSamples = buffer.getNumSamples();
            
            for (int sample = 0; sample < numSamples; ++sample)
            {
                float s = data[sample];
                s = hpMid1_a.process(s);
                s = hpMid1_b.process(s);
                s = hpMid2_a.process(s);
                s = hpMid2_b.process(s);
                s = lpMid1_a.process(s);
                s = lpMid1_b.process(s);
                s = lpMid2_a.process(s);
                s = lpMid2_b.process(s);
                data[sample] = s;
            }
        }
        else
        {
            crossfadeBetweenBanks(buffer, channel);
        }
    }
}

void LinkwitzRileyFilters::processHigh(juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        if (!transitioning)
        {
            auto* data = buffer.getWritePointer(channel);
            const int numSamples = buffer.getNumSamples();
            
            for (int sample = 0; sample < numSamples; ++sample)
            {
                float s = data[sample];
                s = hpHigh1_a.process(s);
                s = hpHigh1_b.process(s);
                s = hpHigh2_a.process(s);
                s = hpHigh2_b.process(s);
                data[sample] = s;
            }
        }
        else
        {
            crossfadeBetweenBanks(buffer, channel);
        }
    }
}

void LinkwitzRileyFilters::crossfadeBetweenBanks(juce::AudioBuffer<float>& buffer, int channel)
{
    auto* data = buffer.getWritePointer(channel);
    const int numSamples = buffer.getNumSamples();
    
    // Process one sample at a time with crossfade
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float sOld = data[sample];
        float sNew = data[sample];
        
        // Process through old bank
        switch (channel)  // Simpler: process per-channel appropriately
        {
            case 0:  // Assuming low band for demo
                sOld = oldBank.lpLow1_a.process(sOld);
                sOld = oldBank.lpLow1_b.process(sOld);
                sOld = oldBank.lpLow2_a.process(sOld);
                sOld = oldBank.lpLow2_b.process(sOld);
                break;
            // ... full per-band routing would go here
        }
        
        // Process through new bank
        switch (channel)
        {
            case 0:
                sNew = newBank.lpLow1_a.process(sNew);
                sNew = newBank.lpLow1_b.process(sNew);
                sNew = newBank.lpLow2_a.process(sNew);
                sNew = newBank.lpLow2_b.process(sNew);
                break;
            // ... full per-band routing
        }
        
        // Crossfade
        float alpha = transitionAlpha;
        data[sample] = sOld * (1.0f - alpha) + sNew * alpha;
        
        // Advance transition
        transitionAlpha += 1.0f / transitionSamplesRemaining;
        if (transitionAlpha >= 1.0f)
        {
            transitionAlpha = 1.0f;
            transitioning = false;
            
            // Finalize: copy new bank coefficients to active
            lpLow1_a = newBank.lpLow1_a;
            lpLow1_b = newBank.lpLow1_b;
            lpLow2_a = newBank.lpLow2_a;
            lpLow2_b = newBank.lpLow2_b;
            hpMid1_a = newBank.hpMid1_a;
            hpMid1_b = newBank.hpMid1_b;
            hpMid2_a = newBank.hpMid2_a;
            hpMid2_b = newBank.hpMid2_b;
            lpMid1_a = newBank.lpMid1_a;
            lpMid1_b = newBank.lpMid1_b;
            lpMid2_a = newBank.lpMid2_a;
            lpMid2_b = newBank.lpMid2_b;
            hpHigh1_a = newBank.hpHigh1_a;
            hpHigh1_b = newBank.hpHigh1_b;
            hpHigh2_a = newBank.hpHigh2_a;
            hpHigh2_b = newBank.hpHigh2_b;
            
            lowCutoff = targetLowCutoff;
            highCutoff = targetHighCutoff;
        }
    }
}

float LinkwitzRileyFilters::processLowSample(float sample)
{
    sample = lpLow1_a.process(sample);
    sample = lpLow1_b.process(sample);
    sample = lpLow2_a.process(sample);
    sample = lpLow2_b.process(sample);
    return sample;
}

float LinkwitzRileyFilters::processMidSample(float sample)
{
    sample = hpMid1_a.process(sample);
    sample = hpMid1_b.process(sample);
    sample = hpMid2_a.process(sample);
    sample = hpMid2_b.process(sample);
    sample = lpMid1_a.process(sample);
    sample = lpMid1_b.process(sample);
    sample = lpMid2_a.process(sample);
    sample = lpMid2_b.process(sample);
    return sample;
}

float LinkwitzRileyFilters::processHighSample(float sample)
{
    sample = hpHigh1_a.process(sample);
    sample = hpHigh1_b.process(sample);
    sample = hpHigh2_a.process(sample);
    sample = hpHigh2_b.process(sample);
    return sample;
}

bool LinkwitzRileyFilters::isTransitioning() const
{
    return transitioning;
}

float LinkwitzRileyFilters::getTransitionProgress() const
{
    return transitionAlpha;
}

// LinkwitzRileyFilters.cpp

void LinkwitzRileyFilters::resetFilterState()
{
    // Clear all active biquad states (kept for explicit manual resets).
    // NOTE: deliberately NOT called from setLowCutoff/setHighCutoff —
    // zeroing filter state mid-audio causes audible clicks. Keeping
    // state across coefficient changes is the standard "seamless EQ"
    // technique.
    lpLow1_a.reset();  lpLow1_b.reset();
    lpLow2_a.reset();  lpLow2_b.reset();
    hpMid1_a.reset();  hpMid1_b.reset();
    hpMid2_a.reset();  hpMid2_b.reset();
    lpMid1_a.reset();  lpMid1_b.reset();
    lpMid2_a.reset();  lpMid2_b.reset();
    hpHigh1_a.reset(); hpHigh1_b.reset();
    hpHigh2_a.reset(); hpHigh2_b.reset();
}

// juce::dsp::IIR::Coefficients<float>::Ptr LinkwitzRileyFilters::getCurrentLowCoeffs() const
// {
//     // Convert current coefficients to JUCE format (for visualization)
//     auto coeffs = juce::dsp::IIR::Coefficients<float>::makeZero();
//     coeffs->coeffs[0] = currentLowCoeffs[0];
//     coeffs->coeffs[1] = currentLowCoeffs[1];
//     coeffs->coeffs[2] = currentLowCoeffs[2];
//     coeffs->coeffs[3] = currentLowCoeffs[3];
//     coeffs->coeffs[4] = currentLowCoeffs[4];
//     coeffs->coeffs[5] = currentLowCoeffs[4];  // Mirror for symmetric
//     return coeffs;
// }

// juce::dsp::IIR::Coefficients<float>::Ptr LinkwitzRileyFilters::getCurrentMidCoeffs() const
// {
//     auto coeffs = juce::dsp::IIR::Coefficients<float>::makeZero();
//     coeffs->coeffs[0] = currentMidCoeffs[0];
//     coeffs->coeffs[1] = currentMidCoeffs[1];
//     coeffs->coeffs[2] = currentMidCoeffs[2];
//     coeffs->coeffs[3] = currentMidCoeffs[3];
//     coeffs->coeffs[4] = currentMidCoeffs[4];
//     return coeffs;
// }

// juce::dsp::IIR::Coefficients<float>::Ptr LinkwitzRileyFilters::getCurrentHighCoeffs() const
// {
//     auto coeffs = juce::dsp::IIR::Coefficients<float>::makeZero();
//     coeffs->coeffs[0] = currentHighCoeffs[0];
//     coeffs->coeffs[1] = currentHighCoeffs[1];
//     coeffs->coeffs[2] = currentHighCoeffs[2];
//     coeffs->coeffs[3] = currentHighCoeffs[3];
//     coeffs->coeffs[4] = currentHighCoeffs[4];
//     return coeffs;
// }

} // namespace mbsc