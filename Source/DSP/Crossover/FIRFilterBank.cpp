#include "FIRFilterBank.h"
#include "CrossoverManager.h"
#include <cmath>

namespace mbsc {

//==============================================================================
void FIRFilterBank::prepare(double sr, int /*maxBlockSize*/, int numCh)
{
    sampleRate  = sr;
    numChannels = juce::jlimit(1, 8, numCh);

    // Shared input ring: power of two >= tapCount. Every band's
    // convolution reads taps-1 samples back from the write position,
    // so this covers all of them.
    int ringSize = 8;
    while (ringSize < tapCount)
        ringSize <<= 1;

    history.resize((size_t) numChannels);
    for (auto& h : history)
        h.assign((size_t) ringSize, 0.0f);
    histMask = ringSize - 1;
    writePos = 0;

    generateCoefficients();
}

void FIRFilterBank::reset()
{
    for (auto& h : history)
        std::fill(h.begin(), h.end(), 0.0f);
    writePos = 0;
    // Coefficients are configuration, not state — regenerate, don't zero.
    generateCoefficients();
}

//==============================================================================

float FIRFilterBank::sinc(float x)
{
    if (std::abs(x) < 1.0e-7f)
        return 1.0f;
    const float px = juce::MathConstants<float>::pi * x;
    return std::sin(px) / px;
}

float FIRFilterBank::besselI0(float x)
{
    // Modified Bessel function I0(x) via the power-series expansion
    // (Abramowitz & Stegun 9.6.3). Converges in ~20 terms for x <= 20,
    // which covers any sane Kaiser beta.
    float sum = 1.0f;
    float term = 1.0f;
    for (int k = 1; k < 30; ++k)
    {
        term *= (x * x) / (4.0f * (float) k * (float) k);
        sum += term;
        if (term < 1.0e-10f * sum)
            break;
    }
    return sum;
}

void FIRFilterBank::generateCoefficients()
{
    const int N = tapCount;
    const int mid = N / 2;

    // Kaiser window, beta ~ 8.6 => ~ -80 dB stopband (industry-standard
    // choice for pro audio crossovers).
    const float beta = 8.6f;
    const float denomInv = 1.0f / besselI0(beta);

    const float fn1 = lowCutoff  / (float) sampleRate;   // normalized to fs
    const float fn2 = highCutoff / (float) sampleRate;

    // Resize (only setTapCount reallocates in practice; assign() within
    // existing capacity performs no allocation).
    for (auto& c : coeffs)
        c.assign((size_t) N, 0.0f);

    // Build the two WINDOWED prototype low-pass kernels first, then
    // telescope. Windowing the prototypes (not the final bands!) is
    // what keeps the sum exactly delta.
    std::vector<float> lp1((size_t) N), lp2((size_t) N), w((size_t) N);

    for (int k = 0; k < N; ++k)
    {
        const int m = k - mid;

        // Kaiser window
        const float r = (float) (2 * k) / (float) (N - 1) - 1.0f;   // -1..1
        const float arg = beta * std::sqrt(juce::jmax(0.0f, 1.0f - r * r));
        w[(size_t) k] = besselI0(arg) * denomInv;

        // Ideal brick-wall low-pass impulses, fc-normalized:
        //   h[m] = 2*fn * sinc(2*fn*m),  h[0] = 2*fn
        lp1[(size_t) k] = (m == 0 ? 1.0f : sinc(2.0f * fn1 * m)) * 2.0f * fn1;
        lp2[(size_t) k] = (m == 0 ? 1.0f : sinc(2.0f * fn2 * m)) * 2.0f * fn2;
    }

    for (int k = 0; k < N; ++k)
    {
        const size_t i = (size_t) k;
        const float a = lp1[i] * w[i];     // windowed LP @ f1
        const float b = lp2[i] * w[i];     // windowed LP @ f2

        coeffs[0][i] = a;                                    // low
        coeffs[1][i] = b - a;                                // mid
        coeffs[2][i] = ((k == mid) ? 1.0f : 0.0f) - b;       // high
    }
    // low + mid + high == delta exactly. No per-band normalization —
    // normalizing individually DESTROYS the complementarity.
}

//==============================================================================
void FIRFilterBank::setCrossoverFrequencies(float lowFreq, float highFreq)
{
    lowFreq  = juce::jlimit(MIN_CROSSOVER_FREQ, MAX_CROSSOVER_FREQ, lowFreq);
    highFreq = juce::jlimit(MIN_CROSSOVER_FREQ, MAX_CROSSOVER_FREQ, highFreq);

    if (lowFreq >= highFreq)
    {
        const float mid = 0.5f * (lowFreq + highFreq);
        lowFreq  = juce::jmax(MIN_CROSSOVER_FREQ, mid - 100.0f);
        highFreq = juce::jmin(MAX_CROSSOVER_FREQ, mid + 100.0f);
    }

    if (lowFreq != lowCutoff || highFreq != highCutoff)
    {
        lowCutoff  = lowFreq;
        highCutoff = highFreq;
        generateCoefficients();
    }
}

void FIRFilterBank::setLowCutoff(float freqHz)
{
    setCrossoverFrequencies(freqHz, highCutoff);
}

void FIRFilterBank::setHighCutoff(float freqHz)
{
    setCrossoverFrequencies(lowCutoff, freqHz);
}

void FIRFilterBank::setTapCount(int taps)
{
    // Must be even for symmetric (linear-phase) coefficients.
    if (taps % 2 != 0)
        taps += 1;
    taps = juce::jlimit(64, 4096, taps);

    if (taps != tapCount)
    {
        tapCount = taps;

        // Re-ring for the new length and zero-fill (safe from any thread —
        // prepare()'s allocations are only guaranteed at prepare time).
        int ringSize = 8;
        while (ringSize < tapCount)
            ringSize <<= 1;
        for (auto& h : history)
            h.assign((size_t) ringSize, 0.0f);
        histMask = ringSize - 1;
        writePos = 0;

        generateCoefficients();
    }
}

void FIRFilterBank::setQuality(Quality q)
{
    setTapCount(static_cast<int>(q));
}

//==============================================================================
void FIRFilterBank::split(const juce::AudioBuffer<float>& input,
                          std::vector<juce::AudioBuffer<float>>& bandBuffers)
{
    jassert(bandBuffers.size() >= 3);

    const int numSamples = input.getNumSamples();
    const int channels   = juce::jmin(numChannels, input.getNumChannels());

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            // ONE write per input sample feeds all three bands.
            history[(size_t) ch][(size_t) writePos] = input.getSample(ch, n);

            float out[3];
            for (int b = 0; b < 3; ++b)
            {
                const std::vector<float>& c = coeffs[(size_t) b];
                float acc = 0.0f;
                for (int k = 0; k < tapCount; ++k)
                {
                    const int idx = (writePos - k) & histMask;
                    acc += c[(size_t) k] * history[(size_t) ch][(size_t) idx];
                }
                out[b] = acc;
            }

            for (int b = 0; b < 3; ++b)
                if (ch < bandBuffers[(size_t) b].getNumChannels())
                    bandBuffers[(size_t) b].setSample(ch, n, out[b]);
        }
        writePos = (writePos + 1) & histMask;
    }
}

void FIRFilterBank::merge(const std::vector<juce::AudioBuffer<float>>& bandBuffers,
                          juce::AudioBuffer<float>& output)
{
    // Complementary coefficients => plain summation is correct.
    // NO polarity inversion (unlike the LR path's mid-band flip).
    output.clear();
    for (const auto& band : bandBuffers)
        output.addFrom(0, 0, band, 0, 0, output.getNumSamples());
}

const std::vector<float>& FIRFilterBank::getCoefficients(int band) const
{
    jassert(juce::isPositiveAndBelow(band, 3));
    return coeffs[(size_t) band];
}

} // namespace mbsc