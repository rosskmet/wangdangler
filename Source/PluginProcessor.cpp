#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/Compressor/StyleDefaults.h"

namespace mbsc
{

//==============================================================================
WangdanglerAudioProcessor::WangdanglerAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      paramManager (*this)
{
    crossover     = std::make_unique<CrossoverManager> (NUM_BANDS);
    compressor    = std::make_unique<MultiBandCompressor>();
    saturator     = std::make_unique<MultiBandSaturator>();

    bandBuffers.resize ((size_t) NUM_BANDS);

    pullParametersToDSP();

    startTimerHz (30);
}

WangdanglerAudioProcessor::~WangdanglerAudioProcessor() {}

//==============================================================================
const juce::String WangdanglerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool WangdanglerAudioProcessor::acceptsMidi() const  { return false; }
bool WangdanglerAudioProcessor::producesMidi() const { return false; }
bool WangdanglerAudioProcessor::isMidiEffect() const { return false; }
double WangdanglerAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int WangdanglerAudioProcessor::getNumPrograms()             { return 1; }
int WangdanglerAudioProcessor::getCurrentProgram()          { return 0; }
void WangdanglerAudioProcessor::setCurrentProgram (int)     {}
const juce::String WangdanglerAudioProcessor::getProgramName (int) { return {}; }
void WangdanglerAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void WangdanglerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = getTotalNumOutputChannels();

    inputGainLin.reset  (sampleRate, 0.02);
    outputGainLin.reset (sampleRate, 0.02);
    inputGainLin.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (0.0f));
    outputGainLin.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (0.0f));

    crossover->prepare (sampleRate, samplesPerBlock, numChannels);

    for (auto& buf : bandBuffers)
        buf.setSize (numChannels, samplesPerBlock, false, true, false);

    compressor->prepare (sampleRate, samplesPerBlock, numChannels);
    saturator->prepare (sampleRate, samplesPerBlock, numChannels, NUM_BANDS);

    // Latency = compressor lookahead (uniform across bands) + crossover + saturator
    updateLatency();

    pullParametersToDSP();
}

void WangdanglerAudioProcessor::updateLatency()
{
    const int compLatency  = compressor->getLatencySamples();
    const int crossLatency = crossover->getLatencySamples();
    const int satLatency   = saturator->getTotalLatencySamples();

    setLatencySamples (compLatency + crossLatency + satLatency);
}

void WangdanglerAudioProcessor::releaseResources() {}

bool WangdanglerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        || layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
void WangdanglerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels  = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples    = buffer.getNumSamples();

    // ---- Bypass ----
    if (auto* p = paramManager.getRaw (ParamID::global_bypass))
    {
        if (*p > 0.5f)
            return;
    }

    // ---- Parameter sync (message-thread side ran in timer; nothing here) ----

    // ---- Input gain ----
    if (auto* p = paramManager.getRaw (ParamID::input_gain))
        inputGainLin.setTargetValue  (juce::Decibels::decibelsToGain ((float) *p));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
            data[n] *= inputGainLin.getNextValue();
    }

    // ---- Split into bands ----
    crossover->split (buffer, bandBuffers);

    // ---- Per-band processing ----
    for (int band = 0; band < NUM_BANDS; ++band)
    {
        compressor->processBand (band, bandBuffers[(size_t) band]);
        saturator->processBand (band, bandBuffers[(size_t) band]);
    }

    // ---- Merge ----
    crossover->merge (bandBuffers, buffer);

    // ---- Output gain ----
    if (auto* p = paramManager.getRaw (ParamID::output_gain))
        outputGainLin.setTargetValue (juce::Decibels::decibelsToGain ((float) *p));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
            data[n] *= outputGainLin.getNextValue();
    }
}

//==============================================================================
void WangdanglerAudioProcessor::pullParametersToDSP()
{
    auto& pm = getParameterManager();

    // Float/bool parameters: getRaw() is fine — APVTS gives true values
    // for AudioParameterFloat (and 0/1 for AudioParameterBool).
    auto readRaw = [&pm] (const juce::String& id) -> std::optional<float>
    {
        if (auto* atom = pm.getRaw (id))
            return (float) *atom;
        return {};
    };

    // Choice parameters: MUST go through getChoiceIndex — the raw
    // atomic for AudioParameterChoice is normalised (index / n-1).
    auto readChoice = [&pm] (const juce::String& id) -> std::optional<int>
    {
        const int idx = pm.getChoiceIndex (id);
        return (idx >= 0) ? std::optional<int> (idx) : std::nullopt;
    };

    // ---------------------------------------------------------------
    // Crossover
    // ---------------------------------------------------------------
    if (auto v = readRaw (ParamID::crossover_freq_low))
        crossover->setCrossoverFrequency (0, *v);

    if (auto v = readRaw (ParamID::crossover_freq_high))
        crossover->setCrossoverFrequency (1, *v);

    if (auto idx = readChoice (ParamID::filter_type))
        crossover->setFilterType (static_cast<FilterType> (*idx));

    // ---------------------------------------------------------------
    // Stereo link (single-argument API: fold the toggle into the amount)
    // ---------------------------------------------------------------
    bool  linkOn     = false;
    float linkAmount = 0.0f;
    if (auto v = readRaw (ParamID::stereo_link_enabled))        linkOn     = (*v > 0.5f);
    if (auto v = readRaw (ParamID::stereo_link_strength)) linkAmount = *v;

    compressor->setStereoLink (linkOn ? linkAmount : 0.0f);

    // ---------------------------------------------------------------
    // Per-band compressor parameters
    // ---------------------------------------------------------------
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        // Start from the style template, then overwrite with user values.
        // Combo item order { "Vintage", "Opto", "Digital", "Transparent" }
        // matches CompStyle enum order exactly — direct cast is safe.
        int styleIdx = 2;  // CompStyle::Digital
        if (auto idx = readChoice (bandParam (ParamID::comp_style, b)))
            styleIdx = *idx;

        BandCompressorParams bp = styleDefaults (static_cast<CompStyle> (styleIdx));

        if (auto v = readRaw (bandParam (ParamID::comp_threshold, b))) bp.thresholdDb = *v;
        if (auto v = readRaw (bandParam (ParamID::comp_ratio,     b))) bp.ratio       = *v;
        if (auto v = readRaw (bandParam (ParamID::comp_knee,      b))) bp.kneeDb       = *v;
        if (auto v = readRaw (bandParam (ParamID::comp_attack,    b))) bp.attackMs     = *v;
        if (auto v = readRaw (bandParam (ParamID::comp_release,   b))) bp.releaseMs    = *v;
        if (auto v = readRaw (bandParam (ParamID::comp_makeup,     b))) bp.makeupDb    = *v;
        if (auto v = readRaw (bandParam (ParamID::comp_drywet,    b))) bp.dryWetMix    = *v;

        if (auto v = readRaw (bandParam (ParamID::comp_lookahead, b)))   // ⚠️ verify field name
            bp.lookaheadMs = *v;

        // Detection: combo order { RMS, Peak, Hybrid } does NOT match the
        // DetectorType enum order { Peak, RMS, Hybrid } — map explicitly.
        if (auto idx = readChoice (bandParam (ParamID::detection_type, b)))
        {
            switch (*idx)
            {
                case 0:  bp.detector = DetectorType::RMS;    break;
                case 1:  bp.detector = DetectorType::Peak;   break;
                default: bp.detector = DetectorType::Hybrid; break;
            }
        }

        if (auto v = readRaw (bandParam (ParamID::comp_enabled, b)))
            bp.enabled = (*v > 0.5f);

        compressor->setBandParameters (b, bp);
    }

    // ---------------------------------------------------------------
    // Oversampling (saturator owns the engines for now)
    // ---------------------------------------------------------------
    if (auto idx = readChoice (ParamID::oversample))
        saturator->setOversamplingFactor (static_cast<OversamplingEngine::Factor> (*idx));
}

//==============================================================================
bool WangdanglerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* WangdanglerAudioProcessor::createEditor()
{
    return new WangdanglerAudioProcessorEditor (*this);
}

//==============================================================================
void WangdanglerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = paramManager.getAPVTS().copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WangdanglerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (paramManager.getAPVTS().state.getType()))
            paramManager.getAPVTS().replaceState (juce::ValueTree::fromXml (*xml));
}

void WangdanglerAudioProcessor::timerCallback()
{
    pullParametersToDSP();
}

} // namespace mbsc

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mbsc::WangdanglerAudioProcessor();
}