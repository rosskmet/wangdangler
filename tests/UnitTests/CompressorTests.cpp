#include <catch2/catch.hpp>
#include "DSP/Compressor/DetectionLogic.h"

using namespace mbsc;

TEST_CASE("GainComputer - Hard Knee", "[detection]")
{
    GainComputer gc;
    gc.setThreshold(-20.0f);
    gc.setRatio(4.0f);
    gc.setKneeWidth(0.0f);

    // Below threshold: unity
    REQUIRE(gc.computeGain(-40.0f) == Catch::Approx(0.0f));

    // 20 dB over threshold, 4:1 => 20 * (1/4 - 1) = -15 dB
    REQUIRE(gc.computeGain(0.0f) == Catch::Approx(-15.0f).margin(0.001f));

    // 8 dB over, 2:1 => -4 dB
    gc.setRatio(2.0f);
    REQUIRE(gc.computeGain(-12.0f) == Catch::Approx(-4.0f).margin(0.001f));
}

TEST_CASE("GainComputer - Soft Knee Continuity", "[detection]")
{
    GainComputer gc;
    gc.setThreshold(-10.0f);
    gc.setRatio(8.0f);
    gc.setKneeWidth(12.0f);

    // Just inside vs just outside knee should be continuous
    float g1 = gc.computeGain(-10.0f - 6.0f + 0.01f);
    float g2 = gc.computeGain(-10.0f - 6.0f - 0.01f);
    REQUIRE(std::abs(g1 - g2) < 0.01f);

    float g3 = gc.computeGain(-10.0f + 6.0f + 0.01f);
    float g4 = gc.computeGain(-10.0f + 6.0f - 0.01f);
    REQUIRE(std::abs(g3 - g4) < 0.01f);
}

TEST_CASE("LevelDetector - RMS vs Peak on Transient", "[detection]")
{
    LevelDetector rms, peak;
    rms.prepare(48000.0, 512);
    peak.prepare(48000.0, 512);
    rms.setDetectorType(DetectorType::RMS);
    peak.setDetectorType(DetectorType::Peak);

    // Single-sample transient in silence
    float rmsDb = 0.0f, pkDb = 0.0f;
    for (int i = 0; i < 1000; ++i)
    {
        float x = (i == 0) ? 1.0f : 0.0f;
        rmsDb = rms.processSample(x);
        pkDb  = peak.processSample(x);
    }

    // Peak detector still shows elevated level right at transient;
    // RMS with 10ms window will have decayed significantly more
    REQUIRE(pkDb > rmsDb);
}

TEST_CASE("Smoother - Attack Time Accuracy", "[detection]")
{
    GainReductionSmoother sm;
    sm.prepare(48000.0, 512);
    sm.setStyle(CompStyle::Digital);
    sm.setAttackTime(10.0f);

    // Feed constant -10 dB target; after ~1 time constant (480 samples),
    // GR should be about 63% of the way to -10 dB
    for (int i = 0; i < 480; ++i)
        sm.processSample(-10.0f);

    // 63% of -10 dB = -6.3 dB
    REQUIRE(sm.getCurrentGrDb() == Catch::Approx(-6.3f).margin(0.5f));
}