// PluginEditor.h
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace mbsc {

class WangdanglerAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit WangdanglerAudioProcessorEditor(WangdanglerAudioProcessor&);
    ~WangdanglerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void buildBandPanel(int band, juce::Rectangle<int> bounds);

    WangdanglerAudioProcessor& audioProcessor;

    //--- Global controls ---
    juce::Slider inputGainSlider, outputGainSlider;
    juce::ComboBox oversampleBox, filterTypeBox;
    juce::Slider xoverLowSlider, xoverHighSlider;

    //--- Per-band controls (3 x everything) ---
    std::array<juce::Slider, 3> threshold, ratio, attack, release,
                                knee, makeup, compMix, lookahead,
                                satDrive, satMix;
    std::array<juce::ComboBox, 3> compStyle, detection, satType;

    //--- Attachments (own order-of-destruction discipline!) ---
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<Attachment> inputGainAttach, outputGainAttach,
        xoverLowAttach, xoverHighAttach;
    std::unique_ptr<ComboAttach> oversampleAttach, filterTypeAttach;

    std::array<std::unique_ptr<Attachment>, 3> thresholdAttach, ratioAttach,
        attackAttach, releaseAttach, kneeAttach, makeupAttach,
        compMixAttach, lookaheadAttach, satDriveAttach, satMixAttach;
    std::array<std::unique_ptr<ComboAttach>, 3> compStyleAttach,
        detectionAttach, satTypeAttach;

    juce::Label titleLabel;

    // JUCE_DECLARE_NON_COPYABLE_AND_LEAK_DETECTOR(WangdanglerAudioProcessorEditor):
};

} // namespace mbsc