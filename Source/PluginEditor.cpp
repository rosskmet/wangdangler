// PluginEditor.cpp
#include "PluginEditor.h"

namespace mbsc {

WangdanglerAudioProcessorEditor::WangdanglerAudioProcessorEditor(
    WangdanglerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    auto& apvts = audioProcessor.getParameterManager().getAPVTS();

    //--- Header label ---
    titleLabel.setText("MultiBand SatComp",
                       juce::dontSendNotification);
    titleLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    //--- Global controls ---
    for (auto* s : { &inputGainSlider, &outputGainSlider })
    {
        s->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        addAndMakeVisible(*s);
    }
    inputGainAttach  = std::make_unique<Attachment>(apvts, ParamID::input_gain,  inputGainSlider);
    outputGainAttach = std::make_unique<Attachment>(apvts, ParamID::output_gain, outputGainSlider);

    oversampleBox.addItem("OS: Off", 1);
    oversampleBox.addItem("OS: 2x",  2);
    oversampleBox.addItem("OS: 4x",  3);
    oversampleBox.addItem("OS: 8x",  4);
    oversampleBox.setSelectedId(1);
    addAndMakeVisible(oversampleBox);
    oversampleAttach = std::make_unique<ComboAttach>(apvts, ParamID::oversample, oversampleBox);

    filterTypeBox.addItem("IIR", 1);
    filterTypeBox.addItem("FIR", 2);
    filterTypeBox.setSelectedId(1);
    addAndMakeVisible(filterTypeBox);
    filterTypeAttach = std::make_unique<ComboAttach>(apvts, ParamID::filter_type, filterTypeBox);

    for (auto* s : { &xoverLowSlider, &xoverHighSlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 18);
        addAndMakeVisible(*s);
    }
    xoverLowAttach  = std::make_unique<Attachment>(apvts, ParamID::crossover_freq_low,  xoverLowSlider);
    xoverHighAttach = std::make_unique<Attachment>(apvts, ParamID::crossover_freq_high, xoverHighSlider);

    //--- Per-band controls ---
    const juce::String bandNames[3] = { "LOW", "MID", "HIGH" };

    for (int b = 0; b < 3; ++b)
    {
        auto setupSlider = [&](juce::Slider& s, const juce::String& tip)
        {
            s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 55, 16);
            s.setTooltip(tip);
            addAndMakeVisible(s);
        };
        auto setupCombo = [&](juce::ComboBox& c)
        {
            c.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(c);
        };

        auto pid = [&](const char* base) { return bandParam(base, b).c_str(); };

        setupSlider(threshold[b],  "Threshold");  thresholdAttach[b] = std::make_unique<Attachment>(apvts, pid(ParamID::comp_threshold), threshold[b]);
        setupSlider(ratio[b],      "Ratio");      ratioAttach[b]     = std::make_unique<Attachment>(apvts, pid(ParamID::comp_ratio),     ratio[b]);
        setupSlider(attack[b],     "Attack");     attackAttach[b]    = std::make_unique<Attachment>(apvts, pid(ParamID::comp_attack),    attack[b]);
        setupSlider(release[b],    "Release");    releaseAttach[b]   = std::make_unique<Attachment>(apvts, pid(ParamID::comp_release),   release[b]);
        setupSlider(knee[b],       "Knee");       kneeAttach[b]      = std::make_unique<Attachment>(apvts, pid(ParamID::comp_knee),      knee[b]);
        setupSlider(makeup[b],     "Makeup");     makeupAttach[b]    = std::make_unique<Attachment>(apvts, pid(ParamID::comp_makeup),     makeup[b]);
        setupSlider(compMix[b],    "Comp Mix");   compMixAttach[b]   = std::make_unique<Attachment>(apvts, pid(ParamID::comp_drywet),    compMix[b]);
        setupSlider(lookahead[b],  "Lookahead");  lookaheadAttach[b] = std::make_unique<Attachment>(apvts, pid(ParamID::comp_lookahead), lookahead[b]);
        setupSlider(satDrive[b],   "Drive");      satDriveAttach[b]  = std::make_unique<Attachment>(apvts, pid(ParamID::sat_drive),     satDrive[b]);
        setupSlider(satMix[b],     "Sat Mix");    satMixAttach[b]    = std::make_unique<Attachment>(apvts, pid(ParamID::sat_drywet),    satMix[b]);

        setupCombo(compStyle[b]);
        compStyle[b].addItem("Vintage", 1); compStyle[b].addItem("Opto", 2);
        compStyle[b].addItem("Digital", 3); compStyle[b].addItem("Transparent", 4);
        compStyle[b].setSelectedId(3);
        compStyleAttach[b] = std::make_unique<ComboAttach>(apvts, pid(ParamID::comp_style), compStyle[b]);

        setupCombo(detection[b]);
        detection[b].addItem("RMS", 1); detection[b].addItem("Peak", 2);
        detection[b].addItem("Hybrid", 3);
        detection[b].setSelectedId(2);
        detectionAttach[b] = std::make_unique<ComboAttach>(apvts, pid(ParamID::detection_type), detection[b]);

        setupCombo(satType[b]);
        satType[b].addItem("Tube", 1); satType[b].addItem("Tape", 2);
        satType[b].addItem("Solid State", 3); satType[b].addItem("Wavefolder", 4);
        satTypeAttach[b] = std::make_unique<ComboAttach>(apvts, pid(ParamID::sat_type), satType[b]);
    }

    setSize(1080, 620);
}

WangdanglerAudioProcessorEditor::~WangdanglerAudioProcessorEditor() = default;

void WangdanglerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2b2b30));

    // Band panel separators
    g.setColour(juce::Colour(0xff3a3a42));
    for (int i = 1; i < 3; ++i)
        g.fillRect(getWidth() * i / 3 - 1, 100, 2, getHeight() - 100);
}

void WangdanglerAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(100);
    auto footer = bounds.removeFromBottom(70);

    // Header: title + globals
    titleLabel.setBounds(header.removeFromLeft(260).reduced(10));

    auto globalsRow = header.reduced(8);
    inputGainSlider .setBounds(globalsRow.removeFromLeft(80));
    outputGainSlider.setBounds(globalsRow.removeFromLeft(80));
    oversampleBox   .setBounds(globalsRow.removeFromLeft(80));
    filterTypeBox   .setBounds(globalsRow.removeFromLeft(80));

    // Footer: crossover sliders
    auto xr = footer.reduced(8);
    xoverLowSlider .setBounds(xr.removeFromLeft(320).reduced(4));
    xoverHighSlider.setBounds(xr.removeFromLeft(320).reduced(4));

    // Three equal band columns
    auto bandArea = bounds;
    for (int b = 0; b < 3; ++b)
    {
        auto col = bandArea.removeFromLeft(bandArea.getWidth() / 3).reduced(6);

        // Style row on top
        compStyle[b]  .setBounds(col.removeFromTop(24));
        detection[b]  .setBounds(col.removeFromTop(24));
        satType[b]    .setBounds(col.removeFromTop(24));

        // Two rows of five rotary knobs each
        for (int r = 0; r < 2; ++r)
        {
            auto row = col.removeFromTop(90);
            juce::Slider* rowKnobs[5] = {
                r == 0 ? &threshold[b] : &knee[b],
                r == 0 ? &ratio[b]     : &makeup[b],
                r == 0 ? &attack[b]    : &compMix[b],
                r == 0 ? &release[b]   : &lookahead[b],
                r == 0 ? &satDrive[b]  : &satMix[b]
            };
            for (auto* knob : rowKnobs)
            {
                float w = (float) row.getWidth() / 5.0f;
                knob->setBounds(row.removeFromLeft((int) w).reduced(2));
            }
        }
    }
}

} // namespace mbsc