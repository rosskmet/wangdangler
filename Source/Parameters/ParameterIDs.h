#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mbsc {

struct ParamID
{
    // Global
    static constexpr const char* input_gain = "input_gain";
    static constexpr const char* output_gain = "output_gain";
    static constexpr const char* oversample = "oversample";
    static constexpr const char* stereo_link_enabled = "stereo_link_enabled";
    static constexpr const char* stereo_link_strength = "stereo_link_strength";
    
    // Crossover
    static constexpr const char* crossover_freq_low = "crossover_freq_low";
    static constexpr const char* crossover_freq_high = "crossover_freq_high";
    static constexpr const char* filter_type = "filter_type";
    static constexpr const char* filter_order = "filter_order";
    
    // Per-band parameters (append band number: _b0, _b1, _b2)
    static constexpr const char* comp_enabled = "_comp_enabled";
    static constexpr const char* comp_threshold = "_comp_threshold";
    static constexpr const char* comp_ratio = "_comp_ratio";
    static constexpr const char* comp_attack = "_comp_attack";
    static constexpr const char* comp_release = "_comp_release";
    static constexpr const char* comp_knee = "_comp_knee";
    static constexpr const char* comp_makeup = "_comp_makeup";
    static constexpr const char* comp_drywet = "_comp_drywet";
    static constexpr const char* comp_style = "_comp_style";
    static constexpr const char* comp_lookahead = "_comp_lookahead";
    static constexpr const char* detection_type = "_detection_type";
    
    static constexpr const char* sat_enabled = "_sat_enabled";
    static constexpr const char* sat_drive = "_sat_drive";
    static constexpr const char* sat_drywet = "_sat_drywet";
    static constexpr const char* sat_type = "_sat_type";
    static constexpr const char* sat_tone = "_sat_tone";
    static constexpr const char* sat_bias = "_sat_bias";  // Tube-specific
    
    // Utility
    static constexpr const char* global_bypass = "global_bypass";
    static constexpr const char* ab_compare = "ab_compare";
};

// Helper to build full parameter ID with band
inline std::string bandParam(const char* base, int band)
{
    return std::string(base) + "_b" + std::to_string(band);
}

} // namespace mbsc