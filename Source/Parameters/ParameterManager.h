#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIDs.h"
#include "../DSP/DSPConstants.h"

namespace mbsc
{

class ParameterManager
{
public:
    explicit ParameterManager (juce::AudioProcessor& processor)
        : apvts (processor, nullptr, "PARAMS", createLayout())
    {
    }

    juce::AudioProcessorValueTreeState& getAPVTS()             { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const { return apvts; }

    std::atomic<float>* getRaw (const juce::StringRef& id) const
    {
        return apvts.getRawParameterValue (id);
    }

    /**
     * Choice params: getRaw() returns the NORMALIZED 0–1 value
     * (index / numChoices-1), NOT the item index. Casting that raw
     * value to int yields garbage dispatch. This returns the true
     * selected index, or -1 if the id isn't a choice parameter.
     *
     * Message-thread only (getParameter + getIndex do atomic-safe
     * reads; only call from the timer/pull side, never per-audio-block).
     */
    int getChoiceIndex (const juce::StringRef& id) const
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
            return p->getIndex();
        return -1;
    }

private:
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;

        AudioProcessorValueTreeState::ParameterLayout layout;

        // ---------- Global ----------
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::input_gain, 1 }, "Input Gain",
            NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::output_gain, 1 }, "Output Gain",
            NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::oversample, 1 }, "Oversampling",
            StringArray { "Off", "2x", "4x", "8x" }, 0));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamID::global_bypass, 1 }, "Global Bypass", false));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamID::ab_compare, 1 }, "A/B Compare", false));

        // ---------- Stereo linking ----------
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamID::stereo_link_enabled, 1 }, "Stereo Link", true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::stereo_link_strength, 1 }, "Stereo Link Amount",
            NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

        // ---------- Crossover / filters ----------
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::crossover_freq_low, 1 }, "Crossover Low",
            NormalisableRange<float> (MIN_CROSSOVER_FREQ, MAX_CROSSOVER_FREQ,
                                      1.0f, 0.25f),                 // 0.25 skew
            200.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::crossover_freq_high, 1 }, "Crossover High",
            NormalisableRange<float> (MIN_CROSSOVER_FREQ, MAX_CROSSOVER_FREQ,
                                      1.0f, 0.25f),
            2000.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::filter_type, 1 }, "Filter Type",
            StringArray { "IIR (Linkwitz-Riley)", "FIR (Linear Phase)" }, 0));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::filter_order, 1 }, "Filter Order",
            StringArray { "4th (LR4)", "8th (LR8)" }, 0));

        // ---------- Per-band ----------
        for (int b = 0; b < NUM_BANDS; ++b)
            addBandParameters (layout, b);

        return layout;
    }

    static void addBandParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                                   int b)
    {
        using namespace juce;

        const auto bid = [b](const char* base) { return bandParam (base, b); };

        // ----- Compressor -----
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bid (ParamID::comp_enabled), 1 }, "Comp Enable " + String (b), true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_threshold), 1 }, "Threshold " + String (b),
            NormalisableRange<float> (-60.0f, 12.0f, 0.1f), -24.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_ratio), 1 }, "Ratio " + String (b),
            NormalisableRange<float> (1.0f, 20.0f, 0.1f), 2.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_attack), 1 }, "Attack " + String (b),
            NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.25f), 10.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_release), 1 }, "Release " + String (b),
            NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.25f), 150.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_knee), 1 }, "Knee " + String (b),
            NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_makeup), 1 }, "Makeup " + String (b),
            NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_drywet), 1 }, "Comp Mix " + String (b),
            NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::comp_lookahead), 1 }, "Lookahead " + String (b),
            NormalisableRange<float> (0.0f, MAX_LOOKAHEAD_MS, 0.1f), 0.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { bid (ParamID::comp_style), 1 }, "Comp Style " + String (b),
            StringArray { "Vintage", "Opto", "Digital", "Transparent" }, 2)); // Digital

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { bid (ParamID::detection_type), 1 }, "Detection " + String (b),
            StringArray { "RMS", "Peak", "Hybrid" }, 1)); // Peak

        // ----- Saturator -----
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bid (ParamID::sat_enabled), 1 }, "Sat Enable " + String (b), true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::sat_drive), 1 }, "Sat Drive " + String (b),
            NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::sat_drywet), 1 }, "Sat Mix " + String (b),
            NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::sat_tone), 1 }, "Tone " + String (b),
            NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bid (ParamID::sat_bias), 1 }, "Bias " + String (b),
            NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { bid (ParamID::sat_type), 1 }, "Sat Type " + String (b),
            StringArray { "Tube", "Tape", "Solid State", "Wavefolder" }, 0)); // Tube
    }
};

} // namespace mbsc