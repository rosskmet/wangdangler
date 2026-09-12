#pragma once

//==============================================================================
/*
    SmoothedParameter.h
    -------------------
    Coefficient-interpolation helpers for click-free filter updates.

    History note: this file originally contained a custom
    mbsc::SmoothedValue<T> template. It was REMOVED in favor of
    juce::SmoothedValue<float> everywhere, because the name collision
    (two classes named SmoothedValue, resolved differently depending
    on include order) caused real build failures. If you need value
    smoothing, use juce::SmoothedValue — never reintroduce a type of
    this name here.
*/
//==============================================================================

#include <cmath>
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace mbsc {

//==============================================================================
/** Convert a time constant to a one-pole coefficient (63.2% convergence). */
inline float timeToCoefficient(float timeSeconds, double sampleRate)
{
    const double fs = juce::jmax(sampleRate, 1.0);
    return static_cast<float>(
        std::exp(-1.0 / (juce::jmax<double>(timeSeconds, 1e-7) * fs)));
}

//==============================================================================
/**
    CoefficientSmoother
    -------------------
    Biquad coefficient interpolation with a stability guarantee.

    Coefficient layout: { b0, b1, b2, a1, a2 }, a0 implicitly 1.0
    (the normalized form used throughout the mbsc DSP code — same
    layout as LinkwitzRileyFilters' coefficient arrays).

    WHY INTERPOLATION NEEDS CARE:
    Naively lerping arbitrary biquad coefficients can pass through
    unstable regions (poles exiting the unit circle) even when BOTH
    endpoints are stable. The stability triangle for (a1, a2):
        |a2| < 1
        a1 + a2 > -1
        a2 - a1 > -1
    is CONVEX, however — so a straight line between two points inside
    it never leaves it. Direct lerp between two stable biquads is
    therefore safe. This class verifies that precondition anyway,
    because defense-in-depth is cheap and coefficient bugs are not.
*/
class CoefficientSmoother
{
public:
    CoefficientSmoother() = default;

    /** Begin a transition from current -> target. */
    void start(const std::array<float, 5>& targetCoeffs, float durationSeconds,
               double sampleRate)
    {
        jassert(isStable(targetCoeffs));

        target    = targetCoeffs;
        position = 0.0f;

        const double fs = juce::jmax(sampleRate, 1.0);
        rampSamples = static_cast<int>(
            juce::jmax(durationSeconds, 0.001f) * fs);
        step = 1.0f / static_cast<float>(rampSamples);
        active = true;
    }

    /** Advance one sample. Returns the current (interpolated)
        coefficients — call exactly once per processed sample. */
    const std::array<float, 5>& getNextCoefficients()
    {
        if (! active)
            return target;

        position += step;
        if (position >= 1.0f)
        {
            current = target;
            active  = false;
            return current;
        }

        const float t = position;
        for (int i = 0; i < 5; ++i)
            current[(size_t) i] = current[(size_t) i] * (1.0f - t)
                                + target[(size_t) i] * t;
        return current;
    }

    /** Snap immediately (no ramp) — for reset/init paths. */
    void snapTo(const std::array<float, 5>& coeffs)
    {
        current = target = coeffs;
        active  = false;
        position = 0.0f;
    }

    bool isActive() const { return active; }
    float getProgress() const { return active ? position : 1.0f; }

    //==================================================================
    /** Stability check: poles inside the unit circle. Both endpoints
        of a transition must satisfy this (see class comment for why
        the path between them is then safe). */
    static bool isStable(const std::array<float, 5>& c)
    {
        const float a1 = c[3];
        const float a2 = c[4];
        return std::abs(a2) < 1.0f - 1e-6f
            && (a1 + a2) > -1.0f + 1e-6f
            && (a2 - a1) > -1.0f + 1e-6f;
    }

private:
    std::array<float, 5> current{};
    std::array<float, 5> target{};
    float position = 1.0f;     // 1.0 = settled
    float step     = 0.0f;
    int   rampSamples = 0;
    bool  active   = false;
};

} // namespace mbsc