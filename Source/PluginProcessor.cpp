#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace IDs
{
    static const juce::String delay    { "delay" };     // ms
    static const juce::String depth    { "depth" };     // cents
    static const juce::String speed    { "speed" };     // Hz
    static const juce::String wave     { "wave" };      // 0 sine, 1 random
    static const juce::String width    { "width" };     // %
    static const juce::String duck     { "duck" };      // %
    static const juce::String dynspeed { "dynspeed" };  // ms release
    static const juce::String dynmod   { "dynmod" };    // %
    static const juce::String feedback { "feedback" };  // %
    static const juce::String vintage  { "vintage" };   // bool
    static const juce::String dry      { "dry" };       // %
    static const juce::String wet      { "wet" };       // %
}

CC2290Processor::CC2290Processor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "params", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout CC2290Processor::createLayout()
{
    using P  = juce::AudioParameterFloat;
    using Pc = juce::AudioParameterChoice;
    using Pb = juce::AudioParameterBool;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<P>  (juce::ParameterID { IDs::delay, 1 }, "Delay",
                                 juce::NormalisableRange<float> (5.0f, 100.0f, 0.1f, 0.5f), 24.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::depth, 1 }, "Mod Depth",
                                 juce::NormalisableRange<float> (0.0f, 25.0f, 0.1f), 6.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("cents")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::speed, 1 }, "Mod Speed",
                                 juce::NormalisableRange<float> (0.05f, 10.0f, 0.01f, 0.35f), 0.4f,
                                 juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<Pc> (juce::ParameterID { IDs::wave, 1 }, "Waveform",
                                 juce::StringArray { "Sine", "Random" }, 1));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::width, 1 }, "Width",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::duck, 1 }, "Ducking",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dynspeed, 1 }, "Dyn Speed",
                                 juce::NormalisableRange<float> (50.0f, 1000.0f, 1.0f, 0.5f), 200.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dynmod, 1 }, "Dyn Mod",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::feedback, 1 }, "Feedback",
                                 juce::NormalisableRange<float> (0.0f, 50.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<Pb> (juce::ParameterID { IDs::vintage, 1 }, "Vintage Tone", true));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dry, 1 }, "Dry",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::wet, 1 }, "Double",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    return l;
}

bool CC2290Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void CC2290Processor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    // max base 100ms * golden ratio + max excursion headroom
    bufLen = juce::nextPowerOfTwo ((int) (sampleRate * 0.35) + 8);
    delayBuf.assign ((size_t) bufLen, 0.0f);
    writePos = 0;
    fbSample = 0.0f;
    env = 0.0f;
    lfoPhase = 0.0;
    randPhaseA = 0.0; randPhaseB = 0.5;
    randValA = randValB = randTargetA = randTargetB = 0.0f;

    const double smoothSec = 0.05;
    for (auto* s : { &smDelayMs, &smWet, &smDry, &smWidth, &smFeedback })
        s->reset (sampleRate, smoothSec);

    auto tone = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 11000.0f, 0.707f);
    toneA.coefficients = tone;
    toneB.coefficients = tone;
    juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };
    toneA.prepare (spec);
    toneB.prepare (spec);
}

float CC2290Processor::readTap (float delaySamples) const
{
    // 4-point Hermite interpolation
    float rp = (float) writePos - delaySamples;
    while (rp < 0.0f) rp += (float) bufLen;
    const int i1 = (int) rp;
    const float frac = rp - (float) i1;
    const int mask = bufLen - 1;
    const float xm1 = delayBuf[(size_t) ((i1 - 1) & mask)];
    const float x0  = delayBuf[(size_t) ( i1      & mask)];
    const float x1  = delayBuf[(size_t) ((i1 + 1) & mask)];
    const float x2  = delayBuf[(size_t) ((i1 + 2) & mask)];
    const float c  = (x1 - xm1) * 0.5f;
    const float v  = x0 - x1;
    const float w  = c + v;
    const float a  = w + v + (x2 - x0) * 0.5f;
    const float bn = w + a;
    return ((((a * frac) - bn) * frac + c) * frac + x0);
}

void CC2290Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    const float pDelay   = apvts.getRawParameterValue (IDs::delay)->load();
    const float pDepth   = apvts.getRawParameterValue (IDs::depth)->load();
    const float pSpeed   = apvts.getRawParameterValue (IDs::speed)->load();
    const bool  useRand  = apvts.getRawParameterValue (IDs::wave)->load() > 0.5f;
    const float pWidth   = apvts.getRawParameterValue (IDs::width)->load() * 0.01f;
    const float pDuck    = apvts.getRawParameterValue (IDs::duck)->load() * 0.01f;
    const float pDynRel  = apvts.getRawParameterValue (IDs::dynspeed)->load();
    const float pDynMod  = apvts.getRawParameterValue (IDs::dynmod)->load() * 0.01f;
    const float pFb      = apvts.getRawParameterValue (IDs::feedback)->load() * 0.01f;
    const bool  vintage  = apvts.getRawParameterValue (IDs::vintage)->load() > 0.5f;
    const float pDry     = apvts.getRawParameterValue (IDs::dry)->load() * 0.01f;
    const float pWet     = apvts.getRawParameterValue (IDs::wet)->load() * 0.01f;

    smDelayMs.setTargetValue (pDelay);
    smWet.setTargetValue (pWet);
    smDry.setTargetValue (pDry);
    smWidth.setTargetValue (pWidth);
    smFeedback.setTargetValue (pFb);

    // automatic depth correction: constant pitch excursion.
    // peak pitch ratio deviation r = 2^(cents/1200) - 1 ; sine tap sweep of
    // amplitude A at f Hz gives peak ratio 2*pi*f*A  =>  A = r / (2*pi*f)
    const float ratio = std::pow (2.0f, pDepth / 1200.0f) - 1.0f;
    float excursionSec = ratio / (juce::MathConstants<float>::twoPi * juce::jmax (0.05f, pSpeed));
    excursionSec = juce::jmin (excursionSec, 0.030f, (pDelay * 0.001f) * 0.6f);
    const float excursionSamp = excursionSec * (float) fs;

    const double lfoInc  = pSpeed / fs;
    // smoothed-random slew coefficient (~half an LFO period to reach target)
    const float slewCoef = 1.0f - std::exp ((float) (-2.0 * pSpeed / fs));

    const float envAtk = 1.0f - std::exp ((float) (-1.0 / (0.005 * fs)));
    const float envRel = 1.0f - std::exp ((float) (-1000.0 / (pDynRel * fs)));

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numCh > 1 ? buffer.getReadPointer (1) : inL;
    float* outL = buffer.getWritePointer (0);
    float* outR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    const int mask = bufLen - 1;
    float inPk = 0.0f, outPk = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float dryL = inL[i];
        const float dryR = inR[i];
        const float mono = 0.5f * (dryL + dryR);

        // envelope follower (for ducking + dynamic modulation)
        const float rect = std::abs (mono);
        env += (rect > env ? envAtk : envRel) * (rect - env);
        const float envNorm = juce::jmin (1.0f, env * 4.0f);   // ~ -12 dBFS => 1.0

        // dynamic modulation: playing harder deepens the modulation
        const float modScale = 1.0f + pDynMod * envNorm * 2.0f;

        // LFOs
        float lfoA, lfoB;
        if (useRand)
        {
            randPhaseA += lfoInc;
            if (randPhaseA >= 1.0) { randPhaseA -= 1.0; randTargetA = rng.nextFloat() * 2.0f - 1.0f; }
            randPhaseB += lfoInc * 1.13;
            if (randPhaseB >= 1.0) { randPhaseB -= 1.0; randTargetB = rng.nextFloat() * 2.0f - 1.0f; }
            randValA += slewCoef * (randTargetA - randValA);
            randValB += slewCoef * (randTargetB - randValB);
            lfoA = randValA * 1.6f;   // make up for slew attenuation
            lfoB = randValB * 1.6f;
        }
        else
        {
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            lfoA = std::sin (juce::MathConstants<float>::twoPi * (float) lfoPhase);
            lfoB = std::sin (juce::MathConstants<float>::twoPi * (float) lfoPhase + 2.0f);
        }

        const float baseSamp = smDelayMs.getNextValue() * 0.001f * (float) fs;
        const float exc      = excursionSamp * modScale;

        float dA = baseSamp + exc * lfoA;
        float dB = baseSamp * 1.618f + exc * lfoB;      // golden-ratio second voice
        dA = juce::jlimit (2.0f, (float) bufLen - 4.0f, dA);
        dB = juce::jlimit (2.0f, (float) bufLen - 4.0f, dB);

        // write input + feedback into the shared delay line
        delayBuf[(size_t) writePos] = mono + fbSample * smFeedback.getNextValue();

        float tapA = readTap (dA);
        float tapB = readTap (dB);
        fbSample = tapA;

        if (vintage)
        {
            tapA = toneA.processSample (tapA);
            tapB = toneB.processSample (tapB);
        }

        // ducking: the double tucks under while you play
        const float duckGain = 1.0f - pDuck * envNorm;

        const float wet   = smWet.getNextValue() * duckGain;
        const float dry   = smDry.getNextValue();
        const float width = smWidth.getNextValue();

        // width 0: single centred voice; width 1: A hard left, B hard right
        const float vA_L = 1.0f;
        const float vA_R = 1.0f - width;          // A moves left as width grows
        const float vB_L = (1.0f - width);        // B moves right as width grows
        const float vB_R = 1.0f;
        const float bGain = width;                // voice B fades in with width

        const float wl = wet * (tapA * vA_L + tapB * vB_L * bGain) * 0.9f;
        const float wr = wet * (tapA * vA_R + tapB * vB_R * bGain) * 0.9f;

        outL[i] = dryL * dry + wl;
        if (outR != nullptr)
            outR[i] = dryR * dry + wr;

        inPk  = juce::jmax (inPk, std::abs (mono));
        outPk = juce::jmax (outPk, std::abs (outL[i]),
                            outR != nullptr ? std::abs (outR[i]) : 0.0f);

        writePos = (writePos + 1) & mask;
    }

    inPeak.store (inPk);
    outPeak.store (outPk);
}

void CC2290Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CC2290Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* CC2290Processor::createEditor()
{
    return new CC2290Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CC2290Processor();
}
