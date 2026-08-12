#pragma once
#include "PluginProcessor.h"

// TC2290-DT-style faceplate: black rack panel, red 7-segment LED value
// displays with nudge arrows, amber-LED buttons, green segment meters.

namespace SevenSeg
{
    // draws `text` (digits, '-', '.') right-aligned into `slots` digit cells
    void draw (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
               juce::Colour lit, juce::Colour dim, int slots = 4);
}

class CC2290LookAndFeel : public juce::LookAndFeel_V4
{
public:
    CC2290LookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool highlighted, bool down) override;
};

// a draggable red LED numeric display (vertical drag, double-click resets)
class SevenSegSlider : public juce::Slider
{
public:
    explicit SevenSegSlider (int decimalsToUse);
    void paint (juce::Graphics&) override;
private:
    int decimals;
};

// label + LED display + up/down nudge arrows
class ParamCell : public juce::Component
{
public:
    ParamCell (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
               const juce::String& labelText, int decimals);
    void resized() override;

    SevenSegSlider slider;

private:
    juce::Label label;
    juce::ShapeButton up { "up", juce::Colour (0xff6a7078), juce::Colour (0xff9aa1a9), juce::Colour (0xffff2f1e) };
    juce::ShapeButton down { "down", juce::Colour (0xff6a7078), juce::Colour (0xff9aa1a9), juce::Colour (0xffff2f1e) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
};

// horizontal green/amber/red LED segment meter
class LevelMeter : public juce::Component
{
public:
    void setLevelDb (float db)   { levelDb = db; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float levelDb = -60.0f;
};

class CC2290Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CC2290Editor (CC2290Processor&);
    ~CC2290Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    CC2290Processor& proc;
    CC2290LookAndFeel lnf;

    std::unique_ptr<ParamCell> speedCell, depthCell, delayCell, feedbackCell,
                               duckCell, dynSpeedCell, dynModCell,
                               dryCell, wetCell, widthCell;

    juce::TextButton sineButton { "SINE" }, randButton { "RANDOM" }, vintageButton { "VINTAGE" };
    std::unique_ptr<juce::ParameterAttachment> waveAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> vintageAttach;

    LevelMeter inMeter, outMeter;
    float inDispDb = -60.0f, outDispDb = -60.0f;

    struct Section { juce::String title; juce::Rectangle<int> bounds; };
    std::vector<Section> sections;
    juce::Rectangle<int> specDisplay;   // decorative "2290" readout

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CC2290Editor)
};
