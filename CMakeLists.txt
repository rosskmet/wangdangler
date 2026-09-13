cmake_minimum_required(VERSION 3.24)

project(Wangdangler VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE Setup - clone or download separately
# Option 1: FetchContent (recommended for portability)
# include(FetchContent)
# FetchContent_Declare(
#     JUCE
#     GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
#     GIT_TAG 7.0.8  # Or latest stable
# )
# FetchContent_MakeAvailable(JUCE)
# Replace the FetchContent block with:
if(NOT DEFINED JUCE_PATH)
    set(JUCE_PATH "$ENV{HOME}/dev/sdks/JUCE" CACHE PATH "~/dev/sdks/JUCE")
endif()
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE SYSTEM)

# Plugin configuration
juce_add_plugin(Wangdangler
    COMPANY_NAME "BadDSP"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    
    # Plugin formats
    # PLUGIN_MANUFACTURER_CODE Your
    # PLUGIN_CODE Mbsc
    
    FORMATS VST3
    
    # Plugin name as it appears in hosts
    PLUGIN_NAME "Wangdangler"
    
    # Icon (path to PNG in Resources)
    # ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Assets/icon.png"
)

# Source files
target_sources(Wangdangler PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    
    # DSP Core
    Source/DSP/Crossover/CrossoverManager.cpp
    Source/DSP/Crossover/IIRFilterBank.cpp
    Source/DSP/Crossover/FIRFilterBank.cpp
    
    Source/DSP/Compressor/MultiBandCompressor.cpp
    Source/DSP/Compressor/DetectionLogic.h
    # Source/DSP/Compressor/CompressionAlgorithms.cpp
    
    Source/DSP/Saturation/MultiBandSaturator.h
    # Source/DSP/Saturation/WaveShapers.cpp
    # Source/DSP/Saturation/TubeModel.cpp
    # Source/DSP/Saturation/TapeModel.cpp
    # Source/DSP/Saturation/SolidStateModel.cpp
    # Source/DSP/Saturation/WavefolderModel.cpp
    
    Source/DSP/Oversampling/OversamplingEngine.h
    # Source/DSP/Oversampling/PolyphaseOversampler.cpp
    # Source/DSP/Oversampling/AdaptiveOversampling.cpp
    
    Source/DSP/Utils/SmoothedParameter.h
    # Source/DSP/Utils/LatencyCompensator.cpp
    # Source/DSP/Utils/StereoLink.cpp
    
    # # Parameters
    Source/Parameters/ParameterManager.h
    Source/Parameters/ParameterIDs.h
    # Source/Parameters/PresetManager.cpp
    
    # # GUI
    # Source/GUI/Components/RotaryKnob.cpp
    # Source/GUI/Components/BandStrip.cpp
    # Source/GUI/Components/CrossoverControl.cpp
    # Source/GUI/Components/LevelMeter.cpp
    # Source/GUI/Components/StyleSelector.cpp
    # Source/GUI/Components/ABBrowser.cpp
    
    # Source/GUI/Layouts/MainLayout.cpp
    # Source/GUI/Layouts/CollapsibleSection.cpp
    # Source/GUI/Layouts/ResponsiveGrid.cpp
    
    # Source/GUI/Visualizers/FrequencyView.cpp
    # Source/GUI/Visualizers/CompressionGraph.cpp
    # Source/GUI/Visualizers/SaturationCurve.cpp
)

# Include directories
target_include_directories(Wangdangler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Parameters
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
)

# JUCE modules - enable what we need
target_link_libraries(Wangdangler PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    # juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(Wangdangler PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    JUCE_WEB_BROWSER=0)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(Wangdangler PUBLIC
        JUCE_USE_SIMD=1
        NDEBUG=1)
endif()

# Testing (optional, requires Catch2 or similar)
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()