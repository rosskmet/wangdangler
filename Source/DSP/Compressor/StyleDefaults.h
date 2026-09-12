// StyleDefaults.h
#pragma once
#include "MultiBandCompressor.h"
#include "DetectionLogic.h"

namespace mbsc {

[[nodiscard]] inline BandCompressorParams styleDefaults (CompStyle style)
{
    BandCompressorParams p;

    switch (style)
    {
        case CompStyle::Vintage:
            p.thresholdDb = -24.0f;  p.ratio = 3.0f;
            p.kneeDb = 12.0f;        // soft, forgiving knee
            p.attackMs = 15.0f;      p.releaseMs = 250.0f;
            break;

        case CompStyle::Opto:
            p.thresholdDb = -24.0f;  p.ratio = 2.5f;
            p.kneeDb = 10.0f;
            p.attackMs = 30.0f;      p.releaseMs = 400.0f;  // slow opto release tail
            break;

        case CompStyle::Digital:
            p.thresholdDb = -20.0f;  p.ratio = 6.0f;
            p.kneeDb = 3.0f;
            p.attackMs = 5.0f;       p.releaseMs = 80.0f;
            break;

        case CompStyle::Transparent:
            p.thresholdDb = -20.0f;  p.ratio = 2.0f;
            p.kneeDb = 1.0f;
            p.attackMs = 3.0f;       p.releaseMs = 50.0f;
            break;
    }

    return p;
}

} // namespace mbsc