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
    static const juce::String fbhicut  { "fbhicut" };   // repeat-only hi-cut choice
    static const juce::String wide     { "wide" };      // bool, wet phase-reverse R
    static const juce::String voice2   { "voice2" };    // bool, golden-ratio 2nd tap
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

    // hardware range starts at 0.1 ms; 1 ms keeps the taps comfortably apart
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::delay, 1 }, "Delay",
                                 juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f, 0.5f), 7.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::depth, 1 }, "Mod Depth",
                                 juce::NormalisableRange<float> (0.0f, 25.0f, 0.1f), 4.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("cents")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::speed, 1 }, "Mod Speed",
                                 juce::NormalisableRange<float> (0.05f, 10.0f, 0.01f, 0.35f), 0.35f,
                                 juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<Pc> (juce::ParameterID { IDs::wave, 1 }, "Waveform",
                                 juce::StringArray { "Sine", "Random" }, 1));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::width, 1 }, "Width",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::duck, 1 }, "Ducking",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    // 2290 dynamic release spans 0.1 s (SPEED 10) to 10 s (SPEED 0.1)
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dynspeed, 1 }, "Dyn Speed",
                                 juce::NormalisableRange<float> (100.0f, 9999.0f, 1.0f, 0.35f), 200.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dynmod, 1 }, "Dyn Mod",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::feedback, 1 }, "Feedback",
                                 juce::NormalisableRange<float> (0.0f, 50.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    // hardware feedback hi-cut: 2/4/8 kHz, 33 kHz = off; shapes repeats only
    l.add (std::make_unique<Pc> (juce::ParameterID { IDs::fbhicut, 1 }, "FB Hi-Cut",
                                 juce::StringArray { "2 kHz", "4 kHz", "8 kHz", "Off" }, 3));
    // the 2290's signature wide mode: wet phase-reversed between L and R
    l.add (std::make_unique<Pb> (juce::ParameterID { IDs::wide, 1 }, "Wide", false));
    // optional golden-ratio second tap (a creative extra — the 2290 is
    // single-voice; keeping it off avoids a second comb source by default)
    l.add (std::make_unique<Pb> (juce::ParameterID { IDs::voice2, 1 }, "Voice 2", false));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dry, 1 }, "Dry",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::wet, 1 }, "Double",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
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
    delayBufL.assign ((size_t) bufLen, 0.0f);
    delayBufR.assign ((size_t) bufLen, 0.0f);
    writePos = 0;
    fbL = fbR = 0.0f;
    env = 0.0f;
    lfoPhase = 0.0;
    randPhaseA = 0.0; randPhaseB = 0.5;
    randValA = randValB = randTargetA = randTargetB = 0.0f;

    const double smoothSec = 0.05;
    for (auto* s : { &smDelayMs, &smWet, &smDry, &smWidth, &smFeedback, &smWideSign, &smVoice2 })
        s->reset (sampleRate, smoothSec);
    smWideSign.setCurrentAndTargetValue (
        apvts.getRawParameterValue ("wide")->load() > 0.5f ? -1.0f : 1.0f);
    smVoice2.setCurrentAndTargetValue (
        apvts.getRawParameterValue ("voice2")->load() > 0.5f ? 1.0f : 0.0f);

    juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };
    fbFiltL.prepare (spec);
    fbFiltR.prepare (spec);
    fbCutIdx = -1;   // force coefficient update on the first block
}

float CC2290Processor::readTap (const std::vector<float>& buf, float delaySamples) const
{
    // 4-point Hermite interpolation
    float rp = (float) writePos - delaySamples;
    while (rp < 0.0f) rp += (float) bufLen;
    const int i1 = (int) rp;
    const float frac = rp - (float) i1;
    const int mask = bufLen - 1;
    const float xm1 = buf[(size_t) ((i1 - 1) & mask)];
    const float x0  = buf[(size_t) ( i1      & mask)];
    const float x1  = buf[(size_t) ((i1 + 1) & mask)];
    const float x2  = buf[(size_t) ((i1 + 2) & mask)];
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
    const int   pFbCut   = (int) apvts.getRawParameterValue (IDs::fbhicut)->load();
    const bool  pWide    = apvts.getRawParameterValue (IDs::wide)->load() > 0.5f;
    const bool  pVoice2  = apvts.getRawParameterValue (IDs::voice2)->load() > 0.5f;
    const float pDry     = apvts.getRawParameterValue (IDs::dry)->load() * 0.01f;
    const float pWet     = apvts.getRawParameterValue (IDs::wet)->load() * 0.01f;

    smDelayMs.setTargetValue (pDelay);
    smWet.setTargetValue (pWet);
    smDry.setTargetValue (pDry);
    smWidth.setTargetValue (pWidth);
    smFeedback.setTargetValue (pFb);
    smWideSign.setTargetValue (pWide ? -1.0f : 1.0f);
    smVoice2.setTargetValue (pVoice2 ? 1.0f : 0.0f);

    if (pFbCut != fbCutIdx)
    {
        fbCutIdx = pFbCut;
        if (fbCutIdx < 3)
        {
            static const float cutHz[3] = { 2000.0f, 4000.0f, 8000.0f };
            auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, cutHz[fbCutIdx], 0.707f);
            fbFiltL.coefficients = c;
            fbFiltR.coefficients = c;
        }
        fbFiltL.reset();
        fbFiltR.reset();
    }
    const bool fbCutOn = fbCutIdx < 3;

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

        // ducking: the double tucks under while you play; the 2290 ducks the
        // feedback path with the same envelope, so we do too
        const float duckGain = 1.0f - pDuck * envNorm;

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

        // write input + feedback into the per-channel delay lines; a mono
        // input fills both lines identically, so mono behaviour is unchanged.
        // the hi-cut filter sits in the feedback path only, so the first echo
        // stays full-band and just the repeats darken, as on the hardware
        const float fbAmt = smFeedback.getNextValue() * duckGain;
        const float fbInL = fbCutOn ? fbFiltL.processSample (fbL) : fbL;
        const float fbInR = fbCutOn ? fbFiltR.processSample (fbR) : fbR;
        delayBufL[(size_t) writePos] = dryL + fbInL * fbAmt;
        delayBufR[(size_t) writePos] = dryR + fbInR * fbAmt;

        const float tapAL = readTap (delayBufL, dA);
        const float tapAR = readTap (delayBufR, dA);
        const float tapBL = readTap (delayBufL, dB);
        const float tapBR = readTap (delayBufR, dB);
        fbL = tapAL;
        fbR = tapAR;

        const float wet   = smWet.getNextValue() * duckGain;
        const float dry   = smDry.getNextValue();
        float width       = smWidth.getNextValue();
        const float v2    = smVoice2.getNextValue();
        if (outR == nullptr)
            width = 0.0f;   // the dry/wet split needs two channels

        // width is the 2290-style split: the dry walks left while the delayed
        // voice walks right, so at full width the dry and the double never sum
        // on the same channel — summing them is what combs like a flanger at
        // short delay times. voice B (optional) takes the opposite side.
        const float dryPanL = 1.0f;
        const float dryPanR = 1.0f - width;
        const float vA_L = 1.0f - width;
        const float vA_R = 1.0f;
        const float vB_L = 1.0f;
        const float vB_R = 1.0f - width;

        const float wl = wet * (tapAL * vA_L + tapBL * vB_L * v2 * 0.8f) * 0.9f;
        const float wr = wet * (tapAR * vA_R + tapBR * vB_R * v2 * 0.8f) * 0.9f;

        // wide mode: wet phase-reversed on the right, per the 2290's stereo
        // trick ("pleasantly broad but not monocompatible"); smoothed so
        // toggling doesn't click
        const float wideSign = smWideSign.getNextValue();

        outL[i] = dryL * dry * dryPanL + wl;
        if (outR != nullptr)
            outR[i] = dryR * dry * dryPanR + wr * wideSign;

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
