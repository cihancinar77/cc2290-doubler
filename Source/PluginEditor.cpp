#include "PluginEditor.h"

namespace
{
    const juce::Colour faceTop    { 0xff23262b };
    const juce::Colour faceBottom { 0xff141619 };
    const juce::Colour frameDark  { 0xff08090a };
    const juce::Colour sectionLn  { 0xff41454c };
    const juce::Colour labelCol   { 0xffc3c9d1 };
    const juce::Colour titleCol   { 0xffe8ebef };
    const juce::Colour ledRed     { 0xffff2f1e };
    const juce::Colour ledRedDim  { 0x30ff2f1e };
    const juce::Colour dispBg     { 0xff170908 };
    const juce::Colour ledAmber   { 0xffffb400 };
}

//==============================================================================
namespace SevenSeg
{
    // segment bits: A=1 B=2 C=4 D=8 E=16 F=32 G=64
    static int maskFor (juce::juce_wchar c)
    {
        switch (c)
        {
            case '0': return 63;   case '1': return 6;    case '2': return 91;
            case '3': return 79;   case '4': return 102;  case '5': return 109;
            case '6': return 125;  case '7': return 7;    case '8': return 127;
            case '9': return 111;  case '-': return 64;   default:  return 0;
        }
    }

    static void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, int mask)
    {
        const float w = r.getWidth(), h = r.getHeight();
        const float t = juce::jmin (h * 0.15f, w * 0.26f);
        const float x = r.getX(), y = r.getY();
        const float vh = (h - t) * 0.5f - t * 0.9f;
        const float rad = t * 0.35f;

        auto drawSeg = [&] (int bit, float sx, float sy, float sw, float sh)
        {
            if ((mask & bit) != 0)
                g.fillRoundedRectangle (sx, sy, sw, sh, rad);
        };

        drawSeg (1,  x + t * 0.8f, y,                    w - t * 1.6f, t);              // A
        drawSeg (64, x + t * 0.8f, y + (h - t) * 0.5f,   w - t * 1.6f, t);              // G
        drawSeg (8,  x + t * 0.8f, y + h - t,            w - t * 1.6f, t);              // D
        drawSeg (32, x,            y + t * 0.7f,         t,            vh);             // F
        drawSeg (2,  x + w - t,    y + t * 0.7f,         t,            vh);             // B
        drawSeg (16, x,            y + h * 0.5f + t * 0.4f, t,         vh);             // E
        drawSeg (4,  x + w - t,    y + h * 0.5f + t * 0.4f, t,         vh);             // C
    }

    void draw (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
               juce::Colour lit, juce::Colour dim, int slots)
    {
        struct Slot { juce::juce_wchar c = ' '; bool dot = false; };
        std::vector<Slot> chars;
        for (auto ch : text)
        {
            if (ch == '.' && ! chars.empty())
                chars.back().dot = true;
            else
                chars.push_back ({ ch, false });
        }

        const float pitch  = area.getWidth() / (float) slots;
        const float digitW = pitch * 0.68f;
        const float dotSz  = juce::jmax (2.0f, area.getHeight() * 0.14f);

        for (int i = 0; i < slots; ++i)
        {
            juce::Rectangle<float> cell (area.getX() + pitch * (float) i,
                                         area.getY(), digitW, area.getHeight());
            // ghost segments
            g.setColour (dim);
            drawDigit (g, cell, 127);
            g.setColour (dim);
            g.fillEllipse (cell.getRight() + pitch * 0.06f,
                           area.getBottom() - dotSz, dotSz, dotSz);

            const int idx = (int) chars.size() - slots + i;
            if (idx >= 0 && idx < (int) chars.size())
            {
                g.setColour (lit);
                drawDigit (g, cell, maskFor (chars[(size_t) idx].c));
                if (chars[(size_t) idx].dot)
                    g.fillEllipse (cell.getRight() + pitch * 0.06f,
                                   area.getBottom() - dotSz, dotSz, dotSz);
            }
        }
    }
}

//==============================================================================
CC2290LookAndFeel::CC2290LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, faceBottom);
    setColour (juce::PopupMenu::textColourId, titleCol);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff32363d));
}

void CC2290LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                              const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto base1 = juce::Colour (0xff2c3036);
    auto base2 = juce::Colour (0xff1b1e22);
    if (down)        { base1 = base1.darker (0.3f);   base2 = base2.darker (0.3f); }
    else if (highlighted) { base1 = base1.brighter (0.08f); base2 = base2.brighter (0.08f); }

    g.setGradientFill (juce::ColourGradient (base1, r.getTopLeft(), base2, r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (frameDark);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    // LED lamp
    const float d = 5.0f;
    juce::Rectangle<float> lamp (r.getX() + 6.0f, r.getCentreY() - d * 0.5f, d, d);
    if (b.getToggleState())
    {
        g.setColour (ledAmber.withAlpha (0.35f));
        g.fillEllipse (lamp.expanded (2.5f));
        g.setColour (ledAmber);
    }
    else
    {
        g.setColour (juce::Colour (0xff4a3c14));
    }
    g.fillEllipse (lamp);
}

void CC2290LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? titleCol : labelCol.withAlpha (0.8f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (12), juce::Justification::centred);
}

//==============================================================================
SevenSegSlider::SevenSegSlider (int decimalsToUse) : decimals (decimalsToUse)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setVelocityModeParameters (0.9, 1, 0.09, false);
}

void SevenSegSlider::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (dispBg);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, 3.0f, 1.4f);

    SevenSeg::draw (g, juce::String (getValue(), decimals),
                    r.reduced (7.0f, 6.0f), ledRed, ledRedDim, 4);
}

//==============================================================================
ParamCell::ParamCell (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                      const juce::String& labelText, int decimals)
    : slider (decimals)
{
    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, labelCol);
    addAndMakeVisible (label);
    addAndMakeVisible (slider);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

    juce::Path tri;
    tri.addTriangle (0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f);
    up.setShape (tri, false, true, false);
    juce::Path tri2;
    tri2.addTriangle (0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f);
    down.setShape (tri2, false, true, false);

    up.onClick   = [this] { slider.setValue (slider.getValue() + juce::jmax (slider.getInterval(), 0.01), juce::sendNotificationSync); };
    down.onClick = [this] { slider.setValue (slider.getValue() - juce::jmax (slider.getInterval(), 0.01), juce::sendNotificationSync); };
    up.setRepeatSpeed (350, 60);
    down.setRepeatSpeed (350, 60);
    addAndMakeVisible (up);
    addAndMakeVisible (down);
}

void ParamCell::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
    auto arrows = r.removeFromBottom (16);
    const int aw = 22;
    auto arrowArea = arrows.withSizeKeepingCentre (aw * 2 + 8, 12);
    up.setBounds (arrowArea.removeFromLeft (aw));
    arrowArea.removeFromLeft (8);
    down.setBounds (arrowArea.removeFromLeft (aw));
    slider.setBounds (r.reduced (4, 3));
}

//==============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r.expanded (1.0f), 2.0f);

    const int n = 15;
    const float gap = 2.0f;
    const float segW = (r.getWidth() - gap * (float) (n - 1)) / (float) n;

    for (int i = 0; i < n; ++i)
    {
        const float thresh = -45.0f + 3.0f * (float) i;   // -45 .. -3 dB
        juce::Colour c = i >= 13 ? juce::Colour (0xffff3b30)
                       : i >= 10 ? ledAmber
                                 : juce::Colour (0xff35d34f);
        if (levelDb < thresh)
            c = c.withMultipliedAlpha (0.16f);
        g.setColour (c);
        g.fillRect (r.getX() + (segW + gap) * (float) i, r.getY(), segW, r.getHeight());
    }
}

//==============================================================================
CC2290Editor::CC2290Editor (CC2290Processor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    auto cell = [&] (const char* id, const char* text, int dec)
    { return std::make_unique<ParamCell> (proc.apvts, id, text, dec); };

    speedCell    = cell ("speed",    "SPEED HZ",   2);
    depthCell    = cell ("depth",    "DEPTH CT",   1);
    delayCell    = cell ("delay",    "DELAY MS",   1);
    feedbackCell = cell ("feedback", "FEEDBACK %", 0);
    duckCell     = cell ("duck",     "DUCKING %",  0);
    dynSpeedCell = cell ("dynspeed", "SPEED MS",   0);
    dynModCell   = cell ("dynmod",   "DYN MOD %",  0);
    dryCell      = cell ("dry",      "DRY %",      0);
    wetCell      = cell ("wet",      "DOUBLE %",   0);
    widthCell    = cell ("width",    "WIDTH %",    0);

    for (auto* c : { speedCell.get(), depthCell.get(), delayCell.get(), feedbackCell.get(),
                     duckCell.get(), dynSpeedCell.get(), dynModCell.get(),
                     dryCell.get(), wetCell.get(), widthCell.get() })
        addAndMakeVisible (c);

    // waveform radio pair
    for (auto* b : { &sineButton, &randButton })
        addAndMakeVisible (b);
    auto* waveParam = proc.apvts.getParameter ("wave");
    waveAttach = std::make_unique<juce::ParameterAttachment> (*waveParam,
        [this] (float v)
        {
            const bool rnd = v > 0.5f;
            sineButton.setToggleState (! rnd, juce::dontSendNotification);
            randButton.setToggleState (rnd,  juce::dontSendNotification);
        });
    sineButton.onClick = [this] { waveAttach->setValueAsCompleteGesture (0.0f); };
    randButton.onClick = [this] { waveAttach->setValueAsCompleteGesture (1.0f); };
    waveAttach->sendInitialUpdate();

    wideButton.setClickingTogglesState (true);
    addAndMakeVisible (wideButton);
    wideAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "wide", wideButton);

    voice2Button.setClickingTogglesState (true);
    addAndMakeVisible (voice2Button);
    voice2Attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "voice2", voice2Button);

    // feedback hi-cut cycles 2k -> 4k -> 8k -> off; LED lit while engaged
    addAndMakeVisible (fbCutButton);
    auto* fbCutParam = proc.apvts.getParameter ("fbhicut");
    fbCutAttach = std::make_unique<juce::ParameterAttachment> (*fbCutParam,
        [this] (float v)
        {
            fbCutIdx = juce::jlimit (0, 3, (int) std::lround (v));
            static const char* names[] = { "FB CUT 2K", "FB CUT 4K", "FB CUT 8K", "FB CUT OFF" };
            fbCutButton.setButtonText (names[fbCutIdx]);
            fbCutButton.setToggleState (fbCutIdx < 3, juce::dontSendNotification);
        });
    fbCutButton.onClick = [this] { fbCutAttach->setValueAsCompleteGesture ((float) ((fbCutIdx + 1) % 4)); };
    fbCutAttach->sendInitialUpdate();

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    setSize (830, 460);
    startTimerHz (30);
}

CC2290Editor::~CC2290Editor()
{
    setLookAndFeel (nullptr);
}

void CC2290Editor::timerCallback()
{
    auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -60.0f); };
    inDispDb  = juce::jmax (toDb (proc.inPeak.load()),  inDispDb  - 2.2f);
    outDispDb = juce::jmax (toDb (proc.outPeak.load()), outDispDb - 2.2f);
    inMeter.setLevelDb (inDispDb);
    outMeter.setLevelDb (outDispDb);
}

void CC2290Editor::resized()
{
    sections.clear();
    auto full = getLocalBounds().reduced (8);
    auto area = full.reduced (12, 0);

    area.removeFromTop (40);                                   // header
    auto meterRow = area.removeFromTop (48);
    area.removeFromTop (6);
    auto rowA = area.removeFromTop (146);
    area.removeFromTop (6);
    auto rowB = area.removeFromTop (146);

    // meters
    {
        auto inBox  = meterRow.removeFromLeft (390);
        meterRow.removeFromLeft (6);
        auto outBox = meterRow;
        sections.push_back ({ "INPUT",  inBox });
        sections.push_back ({ "OUTPUT", outBox });
        inMeter.setBounds  (inBox.reduced (10).withTrimmedTop (10));
        outMeter.setBounds (outBox.reduced (10).withTrimmedTop (10));
    }

    auto layoutCells = [] (juce::Rectangle<int> box, std::initializer_list<ParamCell*> cells)
    {
        auto content = box.reduced (8);
        content.removeFromTop (14);
        const int cw = content.getWidth() / (int) cells.size();
        for (auto* c : cells)
            c->setBounds (content.removeFromLeft (cw).reduced (2));
    };

    // row A: MODULATION | DELAY | DYNAMICS
    {
        auto modBox = rowA.removeFromLeft (224);
        rowA.removeFromLeft (6);
        auto delBox = rowA.removeFromLeft (224);
        rowA.removeFromLeft (6);
        auto dynBox = rowA;
        sections.push_back ({ "MODULATION", modBox });
        sections.push_back ({ "DELAY",      delBox });
        sections.push_back ({ "DYNAMICS",   dynBox });
        layoutCells (modBox, { speedCell.get(), depthCell.get() });
        layoutCells (delBox, { delayCell.get(), feedbackCell.get() });
        layoutCells (dynBox, { duckCell.get(), dynSpeedCell.get(), dynModCell.get() });
    }

    // row B: MIX | MODE | SPEC
    {
        auto mixBox = rowB.removeFromLeft (340);
        rowB.removeFromLeft (6);
        auto modeBox = rowB.removeFromLeft (224);
        rowB.removeFromLeft (6);
        auto specBox = rowB;
        sections.push_back ({ "MIX",  mixBox });
        sections.push_back ({ "MODE", modeBox });
        sections.push_back ({ "SPEC", specBox });
        layoutCells (mixBox, { dryCell.get(), wetCell.get(), widthCell.get() });

        auto content = modeBox.reduced (14);
        content.removeFromTop (14);
        const int bh = 26;
        auto pair = [] (juce::Rectangle<int> row, juce::TextButton& a, juce::TextButton& b)
        {
            a.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
            row.removeFromLeft (6);
            b.setBounds (row);
        };
        pair (content.removeFromTop (bh), sineButton, randButton);
        content.removeFromTop (8);
        pair (content.removeFromTop (bh), wideButton, voice2Button);
        content.removeFromTop (8);
        fbCutButton.setBounds (content.removeFromTop (bh));

        specDisplay = specBox.reduced (16).withTrimmedTop (18);
    }
}

void CC2290Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff000000));

    auto face = getLocalBounds().reduced (8).toFloat();
    g.setGradientFill (juce::ColourGradient (faceTop, face.getTopLeft(),
                                             faceBottom, face.getBottomLeft(), false));
    g.fillRoundedRectangle (face, 8.0f);
    g.setColour (frameDark);
    g.drawRoundedRectangle (face, 8.0f, 2.0f);

    // corner screws
    for (auto pt : { face.getTopLeft().translated (14.0f, 14.0f),
                     face.getTopRight().translated (-14.0f, 14.0f),
                     face.getBottomLeft().translated (14.0f, -14.0f),
                     face.getBottomRight().translated (-14.0f, -14.0f) })
    {
        juce::Rectangle<float> s (pt.x - 5.0f, pt.y - 5.0f, 10.0f, 10.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b6066), s.getTopLeft(),
                                                 juce::Colour (0xff26292d), s.getBottomRight(), false));
        g.fillEllipse (s);
        g.setColour (juce::Colour (0xff101214));
        g.drawLine (s.getX() + 2.0f, s.getCentreY(), s.getRight() - 2.0f, s.getCentreY(), 1.4f);
    }

    // header
    auto header = getLocalBounds().reduced (8).removeFromTop (40).toFloat();
    // power LED
    g.setColour (juce::Colour (0xff2f7bff).withAlpha (0.35f));
    g.fillEllipse (header.getX() + 30.0f, header.getCentreY() - 6.0f, 12.0f, 12.0f);
    g.setColour (juce::Colour (0xff9cc4ff));
    g.fillEllipse (header.getX() + 33.0f, header.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setColour (titleCol);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("DYNAMIC DIGITAL DOUBLER", header, juce::Justification::centred);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("CC 2290", header.withTrimmedRight (28.0f), juce::Justification::centredRight);
    g.setColour (sectionLn);
    g.drawLine (header.getX() + 12.0f, header.getBottom(),
                header.getRight() - 12.0f, header.getBottom(), 1.0f);

    // section frames
    for (auto& s : sections)
    {
        auto b = s.bounds.toFloat();
        g.setColour (sectionLn);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);
        g.setColour (labelCol);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (s.title, b.reduced (8.0f, 3.0f).removeFromTop (12.0f),
                    juce::Justification::centredLeft);
    }

    // decorative model number display
    if (! specDisplay.isEmpty())
    {
        auto r = specDisplay.toFloat();
        g.setColour (dispBg);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 3.0f, 1.4f);
        SevenSeg::draw (g, "2290", r.reduced (14.0f, 12.0f), ledRed, ledRedDim, 4);
    }

    // footer strip
    auto footer = getLocalBounds().reduced (8).removeFromBottom (30).toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xff0c0d0f));
    g.fillRoundedRectangle (footer, 6.0f);
    g.setColour (labelCol.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold | juce::Font::italic));
    g.drawText ("c.c. electronic", footer, juce::Justification::centred);
    // version stamp so it's obvious which build the host actually loaded
   #ifdef JucePlugin_VersionString
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("v" JucePlugin_VersionString, footer.reduced (10.0f, 0.0f),
                juce::Justification::centredRight);
   #endif
}
