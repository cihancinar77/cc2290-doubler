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

    // hardware range starts at 0.1 ms; 1 ms keeps the taps comfortably apart.
    // 24 ms default: the ADT band (20-80 ms) where dry+wet stacking reads as
    // doubling, not comb filtering — short splits belong to the kill-dry patch
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::delay, 1 }, "Delay",
                                 juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f, 0.5f), 24.0f,
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
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 75.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::duck, 1 }, "Ducking",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    // 2290 dynamic release spans 0.1 s (SPEED 10) to 10 s (SPEED 0.1)
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dynspeed, 1 }, "Dyn Speed",
                                 juce::NormalisableRange<float> (100.0f, 9999.0f, 1.0f, 0.35f), 400.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dynmod, 1 }, "Dyn Mod",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::feedback, 1 }, "Feedback",
                                 juce::NormalisableRange<float> (0.0f, 50.0f, 1.0f), 0.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    // parameters added after v1.0 get versionHint 2 so Logic's index-based
    // AU automation for the original parameter set stays aligned
    // hardware feedback hi-cut: 2/4/8 kHz, 33 kHz = off; shapes repeats only
    l.add (std::make_unique<Pc> (juce::ParameterID { IDs::fbhicut, 2 }, "FB Hi-Cut",
                                 juce::StringArray { "2 kHz", "4 kHz", "8 kHz", "Off" }, 3));
    // the 2290's signature wide mode: wet phase-reversed between L and R
    l.add (std::make_unique<Pb> (juce::ParameterID { IDs::wide, 2 }, "Wide", false));
    // golden-ratio second tap: the left half of the stereo spread (a creative
    // extra — the real 2290 is single-voice)
    l.add (std::make_unique<Pb> (juce::ParameterID { IDs::voice2, 2 }, "Voice 2", true));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::dry, 1 }, "Dry",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<P>  (juce::ParameterID { IDs::wet, 1 }, "Double",
                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 75.0f,
                                 juce::AudioParameterFloatAttributes().withLabel ("%")));
    return l;
}

bool CC2290Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in.isDisabled() || out.isDisabled()) return false;
    if (in == juce::AudioChannelSet::mono()   && out == juce::AudioChannelSet::mono())   return true;
    // mono -> stereo: how Logic puts a stereo doubler on a mono track
    if (in == juce::AudioChannelSet::mono()   && out == juce::AudioChannelSet::stereo()) return true;
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo()) return true;
    return false;
}

void CC2290Processor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    setLatencySamples (0);   // the delay is the effect, not lookahead
    // max base 100ms * golden ratio + max excursion headroom
    bufLen = juce::nextPowerOfTwo ((int) (sampleRate * 0.35) + 8);
    delayBufL.assign ((size_t) bufLen, 0.0f);
    delayBufR.assign ((size_t) bufLen, 0.0f);
    writePos = 0;
    fbL = fbR = 0.0f;
    env = 0.0f;
    lfoPhase = 0.0;
    randPhaseA = 0.0; randPhaseB = 0.5;
    randValA = randValB = randS1A = randS1B = randTargetA = randTargetB = 0.0f;

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
    // integer/fraction split: writePos never enters the float arithmetic, so
    // the fractional position keeps full precision wherever the write head is
    // (float "writePos - delay" quantised the fraction to as coarse as 1/128
    // sample near the top of the buffer — audible as intermittent fizz)
    const int   di = (int) delaySamples;
    const float x  = 1.0f - (delaySamples - (float) di);   // position in [0,1] between y0 and y1
    const int   mask = bufLen - 1;
    const int   i0 = writePos - di;
    const float ym1 = buf[(size_t) ((i0 - 2) & mask)];
    const float y0  = buf[(size_t) ((i0 - 1) & mask)];
    const float y1  = buf[(size_t) ( i0      & mask)];
    const float y2  = buf[(size_t) ((i0 + 1) & mask)];

    // 4-point 3rd-order "Optimal 2x" resampling kernel (Niemitalo, deip.pdf) —
    // ~42 dB less imaging error than Catmull-Rom on bright modulated material
    const float z = x - 0.5f;
    const float even1 = y1 + y0,  odd1 = y1 - y0;
    const float even2 = y2 + ym1, odd2 = y2 - ym1;
    const float c0 = even1 *  0.45868970870461956f  + even2 * 0.04131401926395584f;
    const float c1 = odd1  *  0.48068024766578432f  + odd2  * 0.17577925564495955f;
    const float c2 = even1 * -0.246185007019907091f + even2 * 0.24614027139700284f;
    const float c3 = odd1  * -0.36030925263849456f  + odd2  * 0.10174985775982505f;
    return ((c3 * z + c2) * z + c1) * z + c0;
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
    // smoothed-random slew: two cascaded one-poles (quarter period each) give
    // a value AND derivative continuous curve — a single pole leaves a slope
    // kink (an instant pitch step, audible as a soft tick) at every new target
    const float slewCoef = 1.0f - std::exp ((float) (-4.0 * pSpeed / fs));

    const float envAtk = 1.0f - std::exp ((float) (-1.0 / (0.005 * fs)));
    const float envRel = 1.0f - std::exp ((float) (-1000.0 / (pDynRel * fs)));

    const int numIns  = getTotalNumInputChannels();
    const int numOuts = juce::jmin (getTotalNumOutputChannels(), numCh);

    // channels beyond the input count hold garbage in AU hosts — clear them,
    // then duplicate the mono input so the stereo path sees real audio on
    // both sides (this is the mono->stereo insert case in Logic)
    for (int ch = numIns; ch < numOuts; ++ch)
        buffer.clear (ch, 0, n);
    if (numIns == 1 && numOuts > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    const bool stereoOut = numOuts > 1;
    const float* inL = buffer.getReadPointer (0);
    const float* inR = stereoOut ? buffer.getReadPointer (1) : inL;
    float* outL = buffer.getWritePointer (0);
    float* outR = stereoOut ? buffer.getWritePointer (1) : nullptr;

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

        // LFOs — both guaranteed within [-1, 1] so the excursion cap below is
        // a structural bound, not a hard clamp that engages and clicks
        float lfoA, lfoB;
        if (useRand)
        {
            randPhaseA += lfoInc;
            if (randPhaseA >= 1.0) { randPhaseA -= 1.0; randTargetA = rng.nextFloat() * 2.0f - 1.0f; }
            randPhaseB += lfoInc * 1.13;
            if (randPhaseB >= 1.0) { randPhaseB -= 1.0; randTargetB = rng.nextFloat() * 2.0f - 1.0f; }
            randS1A  += slewCoef * (randTargetA - randS1A);
            randValA += slewCoef * (randS1A - randValA);
            randS1B  += slewCoef * (randTargetB - randS1B);
            randValB += slewCoef * (randS1B - randValB);
            lfoA = randValA;
            lfoB = randValB;
        }
        else
        {
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            lfoA = std::sin (juce::MathConstants<float>::twoPi * (float) lfoPhase);
            lfoB = -lfoA;   // opposite detune polarity: L drifts sharp as R drifts flat
        }

        const float baseSamp = smDelayMs.getNextValue() * 0.001f * (float) fs;
        // structural margin: with |lfo| <= 1 the modulated tap can never come
        // closer than 3 samples to the write head, so the hard limits below
        // never engage (an engaging clamp is a derivative discontinuity = click)
        const float exc = juce::jmax (0.0f,
            juce::jmin (excursionSamp * modScale, 0.6f * baseSamp, baseSamp - 3.0f));

        float dA = baseSamp + exc * lfoA;
        float dB = baseSamp * 1.618f + exc * lfoB;      // golden-ratio second voice
        dA = juce::jlimit (3.0f, (float) bufLen - 4.0f, dA);
        dB = juce::jlimit (3.0f, (float) bufLen - 4.0f, dB);

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
            width = 0.0f;   // spreading needs two channels

        // dry stays centred; the wet voices spread outwards with width so the
        // image never pulls to one side (a dry-vs-delay split localises to
        // the earlier side — precedence effect). voice A walks right only as
        // voice B fades in on the left to balance it; with voice 2 off the
        // single voice stays centred and width has no effect. the classic
        // 100% wet short split: DRY 0, WIDTH 100, delay a few ms.
        const float spread = width * v2;
        const float vA_L = 1.0f - spread;
        const float vA_R = 1.0f;
        const float vB_L = 1.0f;
        const float vB_R = 1.0f - width;
        const float bGain = spread;

        const float wl = wet * (tapAL * vA_L + tapBL * vB_L * bGain) * 0.9f;
        const float wr = wet * (tapAR * vA_R + tapBR * vB_R * bGain) * 0.9f;

        // wide mode: wet phase-reversed on the right, per the 2290's stereo
        // trick ("pleasantly broad but not monocompatible"); smoothed so
        // toggling doesn't click. Disabled on mono outputs, where the
        // phase-reversed wet would cancel against the summed signal.
        const float wideSign = outR != nullptr ? smWideSign.getNextValue() : 1.0f;

        outL[i] = dryL * dry + wl;
        if (outR != nullptr)
            outR[i] = dryR * dry + wr * wideSign;

        inPk  = juce::jmax (inPk, std::abs (mono));
        outPk = juce::jmax (outPk, std::abs (outL[i]),
                            outR != nullptr ? std::abs (outR[i]) : 0.0f);

        writePos = (writePos + 1) & mask;
    }

    inPeak.store (inPk);
    outPeak.store (outPk);
}

//==============================================================================
// factory presets — recipes grounded in the TC2290 manual's doubling table
// (20-80 ms ADT), the H3000 #231 micropitch patch, and Petrucci's documented
// 7 ms / 100% wet split (kill-dry so nothing combs and nothing pulls sideways)
namespace
{
    struct Preset
    {
        const char* name;
        float delay, depth, speed;  int wave;
        float width, duck, dynspd, dynmod, fb;  int hicut;
        bool wide, v2;  float dry, wet;
    };

    // hicut: 0=2k 1=4k 2=8k 3=off ; wave: 0=sine 1=random
    static const Preset kPresets[] =
    {
        { "Vocal ADT",              24.0f, 6.0f, 0.4f,  1, 75.0f,  0.0f, 400.0f,  0.0f,  0.0f, 3, false, true,  100.0f,  75.0f },
        { "Tight Thickener",        12.0f, 4.0f, 0.8f,  1, 60.0f,  0.0f, 400.0f,  0.0f,  0.0f, 1, false, true,  100.0f,  55.0f },
        { "Micropitch 231",         25.0f, 9.0f, 0.1f,  0, 100.0f, 0.0f, 400.0f,  0.0f,  0.0f, 2, false, true,  100.0f,  80.0f },
        { "Big Rhythm Guitars",     30.0f, 8.0f, 0.3f,  1, 100.0f, 0.0f, 400.0f, 10.0f,  0.0f, 1, false, true,  100.0f,  90.0f },
        { "2290 Dynamic Double",    45.0f, 5.0f, 0.5f,  0, 70.0f, 60.0f, 800.0f, 20.0f, 12.0f, 1, false, true,  100.0f,  70.0f },
        { "Petrucci Split",          7.0f, 4.0f, 0.35f, 1, 100.0f, 0.0f, 400.0f,  0.0f,  0.0f, 3, false, true,    0.0f, 100.0f },
        { "Slapback 90",            90.0f, 2.0f, 0.2f,  1, 35.0f,  0.0f, 400.0f,  0.0f,  8.0f, 0, false, false, 100.0f,  65.0f },
        { "Wet Bus 100%",           24.0f, 7.0f, 0.4f,  1, 100.0f, 0.0f, 400.0f,  0.0f,  0.0f, 1, false, true,    0.0f, 100.0f },
        { "Ultra Wide (Check Mono)",18.0f, 10.0f, 0.6f, 1, 100.0f, 0.0f, 400.0f,  0.0f,  0.0f, 2, true,  true,  100.0f,  85.0f },
        // Fractal Axe-FX "2290 w/ Modulation" tone: 15 ms, no feedback, one
        // sine LFO at 0.35 Hz (depth "low"), 50% mix, wet phase-reversed on
        // the right — here: width 0 (single centred voice) + Wide engaged
        { "Axe-FX 2290 Wide",       15.0f, 5.0f, 0.35f, 0,  0.0f,  0.0f, 400.0f,  0.0f,  0.0f, 3, true,  true,  100.0f, 100.0f },
    };
}

int CC2290Processor::getNumPrograms()
{
    return (int) std::size (kPresets);
}

const juce::String CC2290Processor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? kPresets[(size_t) index].name
                                                              : juce::String();
}

void CC2290Processor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentProgram = index;
    const auto& ps = kPresets[(size_t) index];

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set (IDs::delay,    ps.delay);
    set (IDs::depth,    ps.depth);
    set (IDs::speed,    ps.speed);
    set (IDs::wave,     (float) ps.wave);
    set (IDs::width,    ps.width);
    set (IDs::duck,     ps.duck);
    set (IDs::dynspeed, ps.dynspd);
    set (IDs::dynmod,   ps.dynmod);
    set (IDs::feedback, ps.fb);
    set (IDs::fbhicut,  (float) ps.hicut);
    set (IDs::wide,     ps.wide ? 1.0f : 0.0f);
    set (IDs::voice2,   ps.v2   ? 1.0f : 0.0f);
    set (IDs::dry,      ps.dry);
    set (IDs::wet,      ps.wet);
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
