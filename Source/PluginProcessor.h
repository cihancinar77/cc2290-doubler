#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

// A 2290-style dynamic doubler:
//   short modulated delay (sine / smoothed-random) with automatic depth
//   correction (constant pitch excursion regardless of LFO speed),
//   envelope-driven ducking and dynamic modulation depth, dual-voice
//   stereo spread with golden-ratio second tap.
class CC2290Processor : public juce::AudioProcessor
{
public:
    CC2290Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "CC2290"; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.3; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // block peak levels for the editor's LED meters
    std::atomic<float> inPeak  { 0.0f };
    std::atomic<float> outPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // fractional delay lines, one per channel (each with two read taps) so a
    // stereo input keeps its image in the doubled signal instead of being
    // summed to mono (which also cancelled out-of-phase stereo sources)
    std::vector<float> delayBufL, delayBufR;
    int bufLen = 0, writePos = 0;

    float readTap (const std::vector<float>& buf, float delaySamples) const;

    // LFO state
    double lfoPhase = 0.0;         // sine phase 0..1
    double randPhaseA = 0.0, randPhaseB = 0.5;
    float randTargetA = 0.0f, randTargetB = 0.0f;
    float randValA = 0.0f, randValB = 0.0f;
    juce::Random rng;

    // envelope follower
    float env = 0.0f;

    // smoothed params
    juce::SmoothedValue<float> smDelayMs, smWet, smDry, smWidth, smFeedback;
    float fbL = 0.0f, fbR = 0.0f;

    // vintage tone filters (one per voice per channel)
    juce::dsp::IIR::Filter<float> toneAL, toneAR, toneBL, toneBR;

    double fs = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CC2290Processor)
};
