#include "PluginEditor.h"

using namespace tmx;
using namespace tmui;

static juce::Font fnt (float h, bool bold = false, bool italic = false)
{
    int st = juce::Font::plain; if (bold) st |= juce::Font::bold; if (italic) st |= juce::Font::italic;
    return juce::Font (juce::FontOptions (h, st));
}

//==============================================================================
Look::Look()
{
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff14141f));
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2c3c));
    setColour (juce::PopupMenu::highlightedTextColourId, col::amber);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto tint = juce::Colour ((juce::uint32) (int) s.getProperties().getWithDefault ("tint", (int) col::amber.getARGB()));
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float d = std::min (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const float ar = r * 0.94f, th = std::max (2.2f, d * 0.065f);
    // anneau
    juce::Path t; t.addCentredArc (c.x, c.y, ar, ar, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff2a2c3c)); g.strokePath (t, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    juce::Path v; v.addCentredArc (c.x, c.y, ar, ar, 0, a0, ang, true);
    g.setColour (tint.withAlpha (0.35f)); g.strokePath (v, juce::PathStrokeType (th + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (tint); g.strokePath (v, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // corps
    const float br = r * 0.74f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4c58), c.x - br * 0.4f, c.y - br, juce::Colour (0xff101118), c.x + br * 0.3f, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (juce::Colour (0xff05060a)); g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.5f);
    g.setColour (juce::Colour (0xff22232d)); g.fillEllipse (c.x - br * 0.62f, c.y - br * 0.62f, br * 1.24f, br * 1.24f);
    g.setColour (col::text);
    g.drawLine (c.x + std::sin (ang) * br * 0.25f, c.y - std::cos (ang) * br * 0.25f, c.x + std::sin (ang) * br * 0.92f, c.y - std::cos (ang) * br * 0.92f, std::max (1.8f, d * 0.05f));
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& bt, const juce::Colour&, bool over, bool down)
{
    auto b = bt.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = bt.getToggleState();
    const auto tint = juce::Colour ((juce::uint32) (int) bt.getProperties().getWithDefault ("tint", (int) col::amber.getARGB()));
    if (bt.getProperties().contains ("round"))
    {
        const float prog = (float) bt.getProperties().getWithDefault ("prog", 0.0);
        const bool busy = (bool) bt.getProperties().getWithDefault ("busy", false);
        const auto c = b.getCentre(); const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (busy ? 0xff3a3d4e : 0xffff6a73), c.x, c.y - r, juce::Colour (busy ? 0xff1b1d28 : 0xffc4122a), c.x, c.y + r, false));
        if (down) g.setColour (juce::Colour (0xffa00f22));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.8f : 0.5f)); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 2.0f);
        if (busy) { juce::Path p; p.addCentredArc (c.x, c.y, r, r, 0, 0, juce::MathConstants<float>::twoPi * prog, true); g.setColour (col::amber); g.strokePath (p, juce::PathStrokeType (4.0f)); }
        return;
    }
    if (bt.getProperties().contains ("listen"))
    {
        const float prog = (float) bt.getProperties().getWithDefault ("prog", 0.0);
        const bool busy = (bool) bt.getProperties().getWithDefault ("busy", false);
        const float rad = b.getHeight() * 0.5f;
        g.setColour (col::cyan.withAlpha (busy ? 0.35f : (over ? 0.30f : 0.16f))); g.fillRoundedRectangle (b.expanded (3.0f), rad + 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1d33), b.getX(), 0, juce::Colour (0xff2b1f4a), b.getRight(), 0, false));
        g.fillRoundedRectangle (b, rad);
        if (busy)
        {
            juce::Path clip; clip.addRoundedRectangle (b, rad);
            g.saveState(); g.reduceClipRegion (clip);
            g.setGradientFill (juce::ColourGradient (col::cyan.withAlpha (0.9f), b.getX(), 0, col::spectral.withAlpha (0.9f), b.getRight(), 0, false));
            g.fillRect (b.withWidth (b.getWidth() * juce::jlimit (0.0f, 1.0f, prog)));
            g.restoreState();
        }
        g.setColour (down ? col::amber : col::cyan); g.drawRoundedRectangle (b, rad, 2.0f);
        return;
    }
    if (bt.getProperties().contains ("led"))
    {
        const float r = std::min (b.getHeight() * 0.5f, 6.0f) - 1.0f;
        const auto c = juce::Point<float> (b.getX() + r + 1.0f, b.getCentreY());
        if (on) { g.setColour (tint.withAlpha (0.35f)); g.fillEllipse (c.x - r - 2, c.y - r - 2, 2 * r + 4, 2 * r + 4); }
        g.setColour (on ? tint : juce::Colour (0xff2c3b2c)); g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);
        return;
    }
    g.setColour (on ? tint : (down ? juce::Colour (0xff2a2c3c) : juce::Colour (0xff1c1d29)));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (on ? tint.brighter (0.4f) : (over ? col::dim : juce::Colour (0xff34364a)));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& bt, bool, bool)
{
    const bool on = bt.getToggleState();
    const float fs0 = (float) bt.getProperties().getWithDefault ("fs", 11.0);
    if (bt.getProperties().contains ("listen")) { g.setColour (juce::Colours::white); g.setFont (fnt (fs0, true)); g.drawText (bt.getButtonText(), bt.getLocalBounds(), juce::Justification::centred); return; }
    const bool round = bt.getProperties().contains ("round"), led = bt.getProperties().contains ("led");
    const float fs = (float) bt.getProperties().getWithDefault ("fs", 11.0);
    auto r = bt.getLocalBounds();
    if (led) { g.setColour (col::dim); g.setFont (fnt (fs, true)); g.drawText (bt.getButtonText(), r.withTrimmedLeft ((int) (fs * 1.6f)), juce::Justification::centredLeft); return; }
    g.setColour (round ? juce::Colours::white : (on ? juce::Colour (0xff101216) : col::text));
    g.setFont (fnt (round ? fs * 1.3f : fs, true));
    g.drawText (bt.getButtonText(), r, juce::Justification::centred);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto b = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff1c1d29)); g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colour (0xff34364a)); g.drawRoundedRectangle (b, 4.0f, 1.0f);
    juce::Path p; const float cx = (float) w - 9.0f, cy = (float) h * 0.5f;
    p.addTriangle (cx - 3, cy - 2, cx + 3, cy - 2, cx, cy + 2.5f);
    g.setColour (col::dim); g.fillPath (p);
}

juce::Label* Look::createSliderTextBox (juce::Slider&) { return new juce::Label(); }

//==============================================================================
Knob::Knob (ThaMasterProcessor& p, int idx, juce::Colour t, const juce::String& labelText, int diameter) : dia (diameter), label (labelText), tint (t)
{
    const auto& d = kDefs[idx];
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setTextValueSuffix (d.unit);
    slider.setNumDecimalPlacesToDisplay (d.step < 1.0f ? 1 : 0);
    slider.setDoubleClickReturnValue (true, d.def);
    slider.getProperties().set ("tint", (int) tint.getARGB());
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    att = std::make_unique<SliderAtt> (p.apvts, d.id, slider);
}
void Knob::resized() { slider.setBounds (0, 0, dia, dia); }
void Knob::paint (juce::Graphics& g)
{
    if (label.isEmpty()) return;
    const float f = (float) dia / 44.0f;
    auto pill = juce::Rectangle<int> (dia - 1, 0, getWidth() - dia + 1, (int) (14 * f));
    g.setColour (juce::Colour (0xff05060a)); g.fillRoundedRectangle (pill.toFloat(), 3.0f);
    g.setColour (col::text); g.setFont (fnt (9.5f * f, true));
    g.drawText (slider.getTextFromValue (slider.getValue()), pill, juce::Justification::centred);
    g.setColour (col::text); g.setFont (fnt (10.5f * f, true));
    g.drawText (label, juce::Rectangle<int> (-6, dia + 1, dia + 12, getHeight() - dia - 1), juce::Justification::centredTop);
}

Toggle::Toggle (ThaMasterProcessor& p, int idx, const juce::String& text, juce::Colour t, bool led) : juce::TextButton (text)
{
    setClickingTogglesState (true);
    getProperties().set ("tint", (int) t.getARGB());
    if (led) getProperties().set ("led", true);
    att = std::make_unique<ButtonAtt> (p.apvts, kDefs[idx].id, *this);
}

void Sticker::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f);
    g.addTransform (juce::AffineTransform::rotation (rotation, b.getCentreX(), b.getCentreY()));
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (b.translated (1.5f, 2.0f), 3.0f);
    g.setColour (colour); g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawRoundedRectangle (b, 3.0f, 1.4f);
    g.setColour (juce::Colour (0xff14141c)); g.setFont (fnt (b.getHeight() * 0.68f, true, true));
    g.drawText (text, b.toNearestInt(), juce::Justification::centred);
}

void VMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff05060a)); g.fillRoundedRectangle (b, 2.0f);
    const float f = juce::jlimit (0.f, 1.f, value / range_);
    auto fill = b.reduced (1.5f);
    if (bottom) fill = fill.withTop (fill.getBottom() - fill.getHeight() * f);
    else        fill = fill.withHeight (fill.getHeight() * f);
    g.setColour (colour); g.fillRoundedRectangle (fill, 1.5f);
    g.setColour (juce::Colour (0xff34364a)); g.drawRoundedRectangle (b, 2.0f, 1.0f);
}

void HMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff05060a)); g.fillRoundedRectangle (b, 2.0f);
    auto bar = [&] (float lin, juce::Rectangle<float> r)
    {
        const float db = (float) linToDb ((double) lin), f = juce::jlimit (0.f, 1.f, (db + 48.f) / 48.f);
        g.setColour (db > -1.f ? col::red : db > -9.f ? col::amber : col::green);
        g.fillRect (r.withWidth (r.getWidth() * f));
    };
    auto r = b.reduced (1.5f);
    bar (l, r.removeFromTop (r.getHeight() * 0.5f - 0.5f)); bar (r_, r.withTrimmedTop (1.0f));
    g.setColour (juce::Colour (0xff34364a)); g.drawRoundedRectangle (b, 2.0f, 1.0f);
}

//==============================================================================
void SpectrumView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff04050a)); g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colour (0xff2a2c3c)); g.drawRoundedRectangle (b, 3.0f, 1.0f);
    auto a = b.reduced (3.0f, 4.0f);
    auto xOf = [&] (double f) { return a.getX() + a.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
    const float mid = a.getCentreY(), sc = a.getHeight() * 0.5f / 8.0f;
    g.setColour (juce::Colour (0xff1a1c28));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) g.drawVerticalLine ((int) xOf (f), a.getY(), a.getBottom());
    g.setColour (juce::Colour (0xff2a2c3c)); g.drawHorizontalLine ((int) mid, a.getX(), a.getRight());
    auto& sp = proc.engine.spec;
    static const double fc[Spectral::kNB] = { 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
    g.setColour (col::dim.withAlpha (0.35f));
    for (int i = 0; i < Spectral::kNB; ++i)
    {
        const float v = juce::jlimit (-8.f, 8.f, sp.devOutBands[(size_t) i].load());
        g.fillEllipse (xOf (fc[i]) - 1.5f, mid - v * sc - 1.5f, 3.0f, 3.0f);
    }
    juce::Path line;
    for (int q = 0; q < Spectral::kCurve; ++q)
    {
        const float db = juce::jlimit (-8.f, 8.f, sp.curveOut[(size_t) q].load());
        const float x = xOf (sp.curveF[(size_t) q]), y = mid - db * sc;
        if (q == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    g.setColour (col::cyan.withAlpha (0.25f)); g.strokePath (line, juce::PathStrokeType (5.0f));
    g.setColour (col::cyan); g.strokePath (line, juce::PathStrokeType (2.0f));
}

void MiniAnalyzer::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff04050a)); g.fillRoundedRectangle (b, 3.0f);
    auto a = b.reduced (3.0f);
    const int n = Spectral::kNB; const float w = a.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const float v = juce::jlimit (0.f, 1.f, (proc.engine.spec.devOutBands[(size_t) i].load() + 10.f) / 20.f);
        g.setColour (col::green.withAlpha (0.85f));
        g.fillRect (a.getX() + w * (float) i + 0.5f, a.getBottom() - a.getHeight() * v, w - 1.0f, a.getHeight() * v);
    }
    g.setColour (juce::Colour (0xff34364a)); g.drawRoundedRectangle (b, 3.0f, 1.0f);
}

void StereoView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff04050a)); g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colour (0xff34364a)); g.drawRoundedRectangle (b, 3.0f, 1.0f);
    const float c = proc.engine.corr.load();
    const float cy = b.getCentreY(), r = b.getHeight() * 0.3f;
    const float off = (1.0f - c) * 0.5f * r * 1.4f;        // c=1 : cercles superposes ; c<0 : ecartes
    g.setColour (col::dim); g.setFont (fnt (b.getHeight() * 0.34f));
    g.drawText ("L", b.toNearestInt().removeFromLeft ((int) (b.getWidth() * 0.2f)), juce::Justification::centred);
    g.drawText ("R", b.toNearestInt().removeFromRight ((int) (b.getWidth() * 0.2f)), juce::Justification::centred);
    g.setColour (c > 0.0f ? col::stereo : col::red);
    g.drawEllipse (b.getCentreX() - off - r, cy - r, 2 * r, 2 * r, 1.4f);
    g.drawEllipse (b.getCentreX() + off - r, cy - r, 2 * r, 2 * r, 1.4f);
}

//==============================================================================
ThaMasterEditor::ThaMasterEditor (ThaMasterProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      kIn (p, inGain, col::cyan, "", 24), kOut (p, outGain, col::cyan, "", 24),
      kSpec (p, specAmt, col::spectral, "Amount", 46), kGlue (p, compGlue, col::comp, "Glue", 42), kDetail (p, compDetail, col::comp, "Detail", 42),
      kColor (p, anaColor, col::analog, "Color", 42), kWidth (p, stWidth, col::stereo, "Width", 42), kBass (p, eqBass, col::eq, "Bass", 42), kAir (p, eqAir, col::eq, "Air", 42),
      kClipG (p, clipGain, col::clip, "Gain", 42), kLimG (p, limGain, col::limiter, "Gain", 42), kCeil (p, limCeil, col::limiter, "Ceiling", 22),
      tBypass (p, bypass, "Bypass", col::red, false), tMatch (p, matchOn, "Match", col::cyan, false),
      tSpec (p, specOn, "ON", col::green, true), tComp (p, compOn, "ON", col::green, true), tAna (p, anaOn, "ON", col::green, true), tSt (p, stOn, "ON", col::green, true),
      tEq (p, eqOn, "ON", col::green, true), tClip (p, clipOn, "ON", col::green, true), tLim (p, limOn, "ON", col::green, true),
      tLM (p, stLM, "LM", col::amber, false), tTP (p, limTP, "TP", col::amber, false)
{
    setLookAndFeel (&look);
    setSize (560, 685);
    for (auto* c : std::initializer_list<juce::Component*> { &kIn, &kOut, &kSpec, &kGlue, &kDetail, &kColor, &kWidth, &kBass, &kAir, &kClipG, &kLimG, &kCeil,
                                                              &tBypass, &tMatch, &tSpec, &tComp, &tAna, &tSt, &tEq, &tClip, &tLim, &tLM, &tTP,
                                                              &bListen, &bAuto, &bAutoIn, &bA, &bB, &bAtoB, &bUndo, &bRedo, &bMenu, &cbMon, &cbTarget,
                                                              &sSpec, &sComp, &sAna, &sSt, &sEq, &sClip, &sLim, &spectrum, &stereoView, &mini, &inMeter,
                                                              &grL, &grM, &grH, &compM, &clipM, &limM })
        addAndMakeVisible (c);
    for (auto& m : mode) addAndMakeVisible (m);
    static const char* mn[4] = { "TAPE", "TUBE", "XFMR", "EXC" };
    for (int i = 0; i < 4; ++i)
    {
        mode[i].setButtonText (mn[i]);
        mode[i].getProperties().set ("tint", (int) col::analog.getARGB()); mode[i].getProperties().set ("fs", 7.5);
        mode[i].onClick = [this, i] { setChoice (anaType, i); };
    }
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> { &bAutoIn, &bA, &bB, &bAtoB, &bUndo, &bRedo, &bMenu, &tBypass, &tMatch }) b->getProperties().set ("fs", 10.0);
    tSpec.getProperties().set ("fs", 9.0); tComp.getProperties().set ("fs", 9.0); tAna.getProperties().set ("fs", 9.0); tSt.getProperties().set ("fs", 9.0);
    tEq.getProperties().set ("fs", 9.0); tClip.getProperties().set ("fs", 9.0); tLim.getProperties().set ("fs", 9.0);
    bAutoIn.getProperties().set ("fs", 8.5);

    { juce::StringArray s; s.addTokens (kDefs[monitor].unit, "|", ""); for (int i = 0; i < s.size(); ++i) cbMon.addItem (s[i], i + 1); }
    { juce::StringArray s; s.addTokens (kDefs[target].unit, "|", ""); for (int i = 0; i < s.size(); ++i) cbTarget.addItem (s[i], i + 1); }
    aMon = std::make_unique<ComboAtt> (p.apvts, kDefs[monitor].id, cbMon);
    aTarget = std::make_unique<ComboAtt> (p.apvts, kDefs[target].id, cbTarget);

    bUndo.onClick = [this] { proc.undoMgr.undo(); };
    bRedo.onClick = [this] { proc.undoMgr.redo(); };
    bA.onClick = [this] { if (proc.getAB() != 0) proc.switchAB(); };
    bB.onClick = [this] { if (proc.getAB() != 1) proc.switchAB(); };
    bAtoB.onClick = [this] { proc.copyToOther(); };
    bMenu.onClick = [this]
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Presets");
        for (int i = 0; i < ThaMasterProcessor::presetNames().size(); ++i) m.addItem (100 + i, ThaMasterProcessor::presetNames()[i], true, i == proc.getCurrentPreset());
        m.addSeparator(); m.addItem (1, "Reset tout");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bMenu), [this] (int r) { if (r == 1) proc.resetAll(); else if (r >= 100) proc.applyPreset (r - 100); });
    };
    bListen.getProperties().set ("listen", true); bListen.getProperties().set ("fs", 15.0);
    bListen.onClick = [this] { if (proc.getAutoState() != 0) proc.cancelAuto(); else proc.startListen(); };
    bAuto.getProperties().set ("round", true); bAuto.getProperties().set ("fs", 11.0);
    bAuto.onClick = [this] { if (proc.getAutoState() != 0) proc.cancelAuto(); else proc.startAuto(); };
    bAutoIn.onClick = [this] { if (proc.getAutoState() != 0) proc.cancelAuto(); else proc.startAutoInput(); };
    startTimerHz (30);
}

ThaMasterEditor::~ThaMasterEditor() { stopTimer(); setLookAndFeel (nullptr); }

void ThaMasterEditor::setChoice (int idx, int v)
{
    if (auto* pr = proc.apvts.getParameter (kDefs[idx].id)) { pr->beginChangeGesture(); pr->setValueNotifyingHost (pr->convertTo0to1 ((float) v)); pr->endChangeGesture(); }
}

juce::Rectangle<int> ThaMasterEditor::B (float x, float y, float w, float h) const
{
    return { (int) std::lround (x * S), (int) std::lround (y * S), (int) std::lround (w * S), (int) std::lround (h * S) };
}

void ThaMasterEditor::paint (juce::Graphics& g)
{
    S = (float) getWidth() / 484.0f;
    // fond : degrade nuit + lueurs violettes
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d0d1a), 0, 0, juce::Colour (0xff201838), 0, (float) getHeight(), false));
    g.fillAll();
    for (auto gl : { std::tuple<float, float, float, juce::uint32> { 90, 70, 200, 0x403a2a7a }, { 400, 120, 220, 0x40522a8a }, { 240, 330, 260, 0x2a2a1a5a }, { 60, 520, 200, 0x302a2a7a } })
    {
        const float cx = std::get<0> (gl) * S, cy = std::get<1> (gl) * S, r = std::get<2> (gl) * S;
        g.setGradientFill (juce::ColourGradient (juce::Colour (std::get<3> (gl)), cx, cy, juce::Colours::transparentBlack, cx + r, cy, true));
        g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
    }
    // barre du haut
    g.setColour (juce::Colour (0xff12121c)); g.fillRect (B (0, 0, 484, 29));
    g.setColour (juce::Colour (0xff2a2c3c)); g.drawHorizontalLine ((int) (29 * S), 0, (float) getWidth());
    g.setColour (col::text); g.setFont (fnt (11.5f * S, true));
    g.drawText ("Input", B (32, 4, 30, 20), juce::Justification::centredLeft);
    g.drawText ("Out", B (230, 4, 22, 20), juce::Justification::centredLeft);
    // en-tete : lignes d'onde + titre
    for (int k = 0; k < 3; ++k)
    {
        juce::Path w; const float y0 = (126.0f + 10.0f * k) * S;
        for (int x = 0; x <= getWidth(); x += 3) { const float t = (float) x / (float) getWidth(); const float y = y0 + std::sin (t * 17.0f + k * 1.7f) * 9.0f * S * std::sin (t * 3.1415f); if (x == 0) w.startNewSubPath ((float) x, y); else w.lineTo ((float) x, y); }
        g.setColour ((k == 0 ? col::cyan : k == 1 ? col::spectral : col::comp).withAlpha (0.35f)); g.strokePath (w, juce::PathStrokeType (1.6f * S));
    }
    { const auto tr = B (60, 33, 360, 38);
      g.setColour (juce::Colours::black.withAlpha (0.55f)); g.setFont (fnt (30.0f * S, true, true)); g.drawText ("Fck ThaMASTER", tr.translated (2, 3), juce::Justification::centred);
      g.setColour (col::cyan.withAlpha (0.35f)); g.drawText ("Fck ThaMASTER", tr.translated (-1, 0), juce::Justification::centred);
      g.setColour (col::text); g.drawText ("Fck ThaMASTER", tr, juce::Justification::centred); }
    g.setColour (col::dim); g.setFont (fnt (9.0f * S, true));
    // lignes
    auto row = [&] (juce::Rectangle<int> r, juce::Colour c)
    {
        g.setColour (col::row); g.fillRoundedRectangle (r.toFloat(), 5.0f * S);
        g.setColour (c.withAlpha (0.28f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f * S, 1.0f);
    };
    row (B (14, 170, 456, 75), col::spectral); row (B (14, 250, 456, 65), col::comp);
    row (B (14, 322, 226, 66), col::analog);   row (B (246, 322, 224, 66), col::stereo);
    row (B (14, 395, 456, 67), col::eq);
    row (B (14, 470, 226, 66), col::clip);     row (B (246, 470, 224, 66), col::limiter);
    row (B (14, 545, 456, 44), col::dim);
    // etiquettes
    g.setColour (col::dim); g.setFont (fnt (8.5f * S, true));
    for (int i = 0; i < 3; ++i) g.drawText (i == 0 ? "L" : i == 1 ? "M" : "H", B (190.f + i * 15.f, 300, 10, 10), juce::Justification::centred);
    g.setColour (col::text.withAlpha (0.10f)); g.setFont (fnt (26.0f * S, true));
    g.drawText ("ThaMASTER", B (170, 410, 150, 36), juce::Justification::centred);
    g.setColour (col::dim); g.setFont (fnt (8.5f * S, true));
    g.drawText ("Monitor", B (22, 547, 58, 11), juce::Justification::centred);
    // bandeau de mesures
    const float cw = 64.0f;
    auto cell = [&] (int i, const juce::String& title, const juce::String& val, const juce::String& word, juce::Colour c, juce::Colour wc)
    {
        auto r = B (150.f + i * cw, 548, cw - 2, 38);
        g.setColour (juce::Colour (0xff0a0b12)); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (juce::Colour (0xff2a2c3c)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (col::dim); g.setFont (fnt (7.5f * S, true)); g.drawText (title, r.removeFromTop ((int) (11 * S)), juce::Justification::centred);
        g.setColour (c); g.setFont (fnt (13.0f * S, true)); g.drawText (val, r.removeFromTop ((int) (17 * S)), juce::Justification::centred);
        g.setColour (wc); g.setFont (fnt (7.5f * S, true)); g.drawText (word, r, juce::Justification::centred);
    };
    cell (0, "LUFS S", sLufsS, wS, col::text, cS);
    cell (1, "LUFS I", sLufsI, wI, col::text, cI);
    cell (2, "PEAK", sPeak, "TRUE PEAK", col::text, col::dim);
    cell (3, "DYNAMICS", sDyn, sDynWord, col::text, col::cyan);
    {   // STEREO : barre de position
        auto r = B (150.f + 4 * cw, 548, cw - 2, 38);
        g.setColour (juce::Colour (0xff0a0b12)); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (juce::Colour (0xff2a2c3c)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (col::dim); g.setFont (fnt (7.5f * S, true)); g.drawText ("STEREO", r.removeFromTop ((int) (11 * S)), juce::Justification::centred);
        auto bar = r.removeFromTop ((int) (14 * S)).reduced ((int) (8 * S), (int) (4 * S)).toFloat();
        g.setColour (juce::Colour (0xff1c1d29)); g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (col::cyan); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.05f, 1.0f, 0.5f + 0.5f * stereoPos)), 2.0f);
        g.setColour (stereoPos > -0.1f ? col::green : col::red); g.setFont (fnt (7.5f * S, true)); g.drawText (stereoPos > -0.1f ? "OK" : "PHASE", r, juce::Justification::centred);
    }
}

void ThaMasterEditor::resized()
{
    S = (float) getWidth() / 484.0f;
    auto place = [&] (juce::Component& c, juce::Rectangle<int> r) { c.setBounds (r); };
    // knobs : la taille de la composante = diametre + pastille
    auto knob = [&] (Knob& k, float cx, float top, float w, float h) { place (k, B (cx - k.dia / 2.0f, top, w, h)); k.dia = (int) std::lround (k.dia * S); k.resized(); };
    static bool scaled = false; (void) scaled;

    // barre du haut
    place (kIn, B (6, 3, 24, 24)); kIn.dia = (int) std::lround (24 * S); kIn.resized();
    place (kOut, B (204, 3, 24, 24)); kOut.dia = (int) std::lround (24 * S); kOut.resized();
    place (bAutoIn, B (64, 4, 48, 21)); place (inMeter, B (118, 9, 80, 11));
    place (bUndo, B (254, 4, 20, 21)); place (bRedo, B (278, 4, 20, 21));
    place (bA, B (304, 4, 20, 21)); place (bB, B (326, 4, 20, 21)); place (bAtoB, B (348, 4, 40, 21));
    place (tBypass, B (392, 4, 38, 21)); place (tMatch, B (432, 4, 32, 21)); place (bMenu, B (466, 4, 16, 21));
    place (bListen, B (132, 98, 220, 44)); place (bAuto, B (420, 38, 50, 48)); place (cbTarget, B (424, 90, 46, 16));

    // spectral
    place (sSpec, B (18, 174, 78, 26)); place (tSpec, B (24, 226, 32, 13));
    place (spectrum, B (190, 176, 272, 62));
    // comp
    place (sComp, B (18, 254, 70, 26)); place (tComp, B (24, 296, 32, 13));
    place (grL, B (190, 258, 10, 38)); place (grM, B (205, 258, 10, 38)); place (grH, B (220, 258, 10, 38));
    place (compM, B (308, 258, 8, 42));
    // analog / stereo
    place (sAna, B (18, 326, 78, 26)); place (tAna, B (24, 371, 32, 13));
    for (int i = 0; i < 4; ++i) place (mode[i], B (196, 332.f + i * 12.f, 34, 11));
    place (sSt, B (250, 326, 78, 26)); place (tSt, B (254, 371, 32, 13)); place (tLM, B (298, 368, 24, 14));
    place (stereoView, B (410, 340, 54, 26));
    // eq
    place (sEq, B (18, 399, 56, 26)); place (tEq, B (24, 444, 32, 13));
    // clip / limiter
    place (sClip, B (18, 474, 62, 26)); place (tClip, B (24, 519, 32, 13)); place (clipM, B (204, 480, 11, 46));
    place (sLim, B (250, 474, 78, 26)); place (tLim, B (254, 519, 32, 13)); place (tTP, B (404, 519, 26, 14)); place (limM, B (460, 480, 8, 46));
    // knobs
    place (kSpec, B (104, 178, 80, 62));  kSpec.dia = (int) std::lround (46 * S); kSpec.resized();
    struct KP { Knob* k; float x, y, w, h; };
    for (auto kp : { KP { &kGlue, 106, 256, 80, 58 }, KP { &kDetail, 340, 256, 80, 58 }, KP { &kColor, 106, 326, 80, 60 }, KP { &kWidth, 334, 326, 80, 60 },
                     KP { &kBass, 106, 398, 80, 62 }, KP { &kAir, 334, 398, 80, 62 }, KP { &kClipG, 106, 474, 80, 58 }, KP { &kLimG, 326, 474, 76, 58 }, KP { &kCeil, 406, 476, 54, 40 } })
    {
        place (*kp.k, B (kp.x, kp.y, kp.w, kp.h));
        const int base = kp.k == &kCeil ? 22 : 42;
        kp.k->dia = (int) std::lround (base * S); kp.k->resized();
    }
    (void) knob;
    // bandeau
    place (cbMon, B (22, 560, 58, 20)); place (mini, B (86, 551, 58, 32));
}

void ThaMasterEditor::timerCallback()
{
    auto& e = proc.engine;
    grL.set (e.glue.grOut[0].load()); grM.set (e.glue.grOut[1].load()); grH.set (e.glue.grOut[2].load());
    compM.set (std::max ({ e.glue.grOut[0].load(), e.glue.grOut[1].load(), e.glue.grOut[2].load() }));
    limM.set (e.lim.grOut.load());
    clipM.set (e.clipAct.load());
    inMeter.set (e.peakInL.load(), e.peakInR.load());

    const double s = e.outShort(), in = e.outIntegrated(), tp = (double) e.tpOut.load();
    static const double tg[4] = { -14, -11, -9, -7 };
    const double target = tg[juce::jlimit (0, 3, (int) std::lround (proc.getParam (tmx::target)))];
    auto fmt = [] (double v) { return v < -100 ? juce::String ("--") : juce::String (v, 1); };
    auto judge = [&] (double v, juce::String& w, juce::Colour& c)
    {
        if (v < -100) { w = ""; c = col::dim; return; }
        const double d = v - target;
        if (std::abs (d) < 1.2) { w = "GOOD"; c = col::green; } else if (d < 0) { w = "LOW"; c = col::amber; } else { w = "HIGH"; c = col::amber; }
    };
    sLufsS = fmt (s); sLufsI = fmt (in); judge (s, wS, cS); judge (in, wI, cI);
    sPeak = tp < -100 ? juce::String ("--") : juce::String (tp, 1);
    if (s > -60 && tp > -100)
    {
        const double plr = tp - s;
        sDyn = juce::String (plr, 1);
        sDynWord = plr > 14 ? "OPEN" : plr > 11 ? "PUNCHY" : plr > 8.5 ? "LOUD" : plr > 6.5 ? "DENSE" : "CRUSHED";
    }
    else { sDyn = "--"; sDynWord = ""; }
    stereoPos = e.corr.load();

    const int a = proc.getAutoState();
    bAuto.getProperties().set ("busy", a != 0);
    bAuto.getProperties().set ("prog", proc.getAutoProgress());
    bAuto.setButtonText (a == 1 ? "..." : a == 2 ? "FIX" : "AUTO");
    bListen.getProperties().set ("busy", a == 5 || a == 2); bListen.getProperties().set ("prog", proc.getAutoProgress());
    bListen.setButtonText (a == 5 ? "LISTENING...  " + juce::String ((int) std::ceil (proc.getListenSecondsLeft())) + " s" : a == 2 ? "ADAPTING..." : "SMART LISTEN");
    bListen.repaint();
    bAutoIn.setButtonText (a == 3 ? "..." : "Auto input");
    bAuto.repaint();
    bA.setToggleState (proc.getAB() == 0, juce::dontSendNotification);
    bB.setToggleState (proc.getAB() == 1, juce::dontSendNotification);
    const int ty = (int) std::lround (proc.getParam (tmx::anaType));
    for (int i = 0; i < 4; ++i) mode[i].setToggleState (i == ty, juce::dontSendNotification);
    spectrum.repaint(); stereoView.repaint(); mini.repaint();
    repaint (B (0, 540, 484, 52));
}
