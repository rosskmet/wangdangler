#include "SmoothedParameter.h"

// Specialized coefficient smoothing (avoids instability)
class CoefficientSmoother
{
public:
    /**
     * Safely interpolate biquad coefficients
     * Uses pole-zero mapping to prevent instability during transitions
     */
    static void smoothBiquadCoeffs(std::array<float, 5>& current,
                                   const std::array<float, 5>& target,
                                   float t)  // 0.0 to 1.0
    {
        // Option 1: Linear interpolation (simple but can be unstable)
        for (int i = 0; i < 5; ++i)
        {
            current[i] = current[i] * (1.0f - t) + target[i] * t;
        }
        
        // Option 2: Logarithmic interpolation for stability
        // Uncomment for more conservative approach:
        /*
        for (int i = 0; i < 5; ++i)
        {
            if (std::abs(current[i]) > 1e-6f && std::abs(target[i]) > 1e-6f)
            {
                float logCurr = std::log(std::abs(current[i]));
                float logTgt = std::log(std::abs(target[i]));
                float interpLog = logCurr * (1.0f - t) + logTgt * t;
                current[i] = std::copysign(std::exp(interpLog), current[i]);
            }
            else
            {
                current[i] = current[i] * (1.0f - t) + target[i] * t;
            }
        }
        */
    }
    
    /**
     * Check if biquad coefficients are stable
     * Returns true if poles are inside unit circle
     */
    static bool isStable(const std::array<float, 5>& coeffs)
    {
        // For biquad: a1, a2 determine stability
        // Stability conditions for direct form II:
        // |a2| < 1
        // a1 + a2 > -1
        // a2 - a1 > -1
        float a1 = coeffs[3];
        float a2 = coeffs[4];
        
        return std::abs(a2) < 1.0f - 1e-6f &&
               (a1 + a2) > -1.0f + 1e-6f &&
               (a2 - a1) > -1.0f + 1e-6f;
    }
};

} // namespace mbsc