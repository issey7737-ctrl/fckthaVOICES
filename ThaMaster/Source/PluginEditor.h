#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace tmui
{
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

namespace col
{
    const juce::Colour text    { 0xfff2f1ee };
    const juce::Colour dim     { 0xff9a9db0 };
    const juce::Colour row     { 0xcc0b0c14 };
    const juce::Colour rowLine { 0xff2a2c3c };
    const juce::Colour spectral{ 0xffb48cff };
    const juce::Colour comp    { 0xffff6fb5 };
    const juce::Colour analog  { 0xffffa23c };
    const juce::Colour stereo  { 0xff44d9c8 };
    const juce::Colour eq      { 0xff7fd957 };
    const juce::Colour clip    { 0xffe8d23a };
    const juce::Colour limiter { 0xffff4d5e };
    const juce::Colour cyan    { 0xff54e0ff };
    const juce::Colour blue    { 0xff4d7dff };
    const juce::Colour green   { 0xff62d26f };
    const juce::Colour amber   { 0xffffb938 };
    const juce::Colour red     { 0xffff4d5a };
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.0f, juce::Font::bold)); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// bouton rotatif : corps sombre, anneau colore, pastille de valeur en haut a droite, nom dessous
class Knob : public juce::Component
{
public:
    Knob (ThaMasterProcessor&, int paramIndex, juce::Colour tint, const juce::String& labelText, int diameter);
    void paint (juce::Graphics&) override;
    void resized() override;
    int dia;
    juce::Slider slider;
private:
    juce::String label;
    juce::Colour tint;
    std::unique_ptr<SliderAtt> att;
};

class Toggle : public juce::TextButton
{
public:
    Toggle (ThaMasterProcessor&, int paramIndex, const juce::String& text, juce::Colour tint, bool led);
private:
    std::unique_ptr<ButtonAtt> att;
};

class Sticker : public juce::Component       // etiquette penchee style autocollant
{
public:
    Sticker (const juce::String& t, juce::Colour c, float rot) : text (t), colour (c), rotation (rot) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    juce::String text; juce::Colour colour; float rotation;
};

class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (ThaMasterProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    ThaMasterProcessor& proc;
};

class MiniAnalyzer : public juce::Component
{
public:
    explicit MiniAnalyzer (ThaMasterProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    ThaMasterProcessor& proc;
};

class VMeter : public juce::Component
{
public:
    VMeter (float range, juce::Colour c, bool fromBottom = false) : range_ (range), colour (c), bottom (fromBottom) {}
    void set (float v) { value = v; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float range_, value = 0; juce::Colour colour; bool bottom;
};

class HMeter : public juce::Component       // niveau d'entree (barre horizontale)
{
public:
    void set (float a, float b) { l = a; r_ = b; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float l = 0, r_ = 0;
};

class StereoView : public juce::Component
{
public:
    explicit StereoView (ThaMasterProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    ThaMasterProcessor& proc;
};

class ThaMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ThaMasterEditor (ThaMasterProcessor&);
    ~ThaMasterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void setChoice (int paramIdx, int v);
    juce::Rectangle<int> B (float x, float y, float w, float h) const;   // coordonnees de la maquette 484 x 592

    ThaMasterProcessor& proc;
    Look look;
    float S = 1.0f;

    Knob kIn, kOut, kSpec, kGlue, kDetail, kColor, kWidth, kBass, kAir, kClipG, kLimG, kCeil;
    Toggle tBypass, tMatch, tSpec, tComp, tAna, tSt, tEq, tClip, tLim, tLM, tTP;
    juce::TextButton bListen { "SMART LISTEN" }, bAuto { "AUTO" }, bAutoIn { "Auto input" }, bA { "A" }, bB { "B" }, bAtoB { "A > B" }, bUndo { "<" }, bRedo { ">" }, bMenu { "=" };
    juce::TextButton mode[4];
    juce::ComboBox cbMon, cbTarget;
    std::unique_ptr<ComboAtt> aMon, aTarget;
    Sticker sSpec { "SPECTRAL", col::spectral, -0.07f }, sComp { "COMP", col::comp, 0.05f }, sAna { "ANALOG", col::analog, -0.05f }, sSt { "STEREO", col::stereo, 0.06f },
            sEq { "EQ", col::eq, -0.06f }, sClip { "CLIP", col::clip, 0.05f }, sLim { "LIMITER", col::limiter, -0.06f };
    SpectrumView spectrum { proc };
    MiniAnalyzer mini { proc };
    StereoView stereoView { proc };
    HMeter inMeter;
    VMeter grL { 10, col::blue }, grM { 10, col::blue }, grH { 10, col::red }, compM { 10, col::blue }, clipM { 10, col::amber }, limM { 10, col::red };

    juce::String sLufsS = "--", sLufsI = "--", sPeak = "--", sDyn = "--", sDynWord = "", wS = "", wI = "";
    float stereoPos = 0;
    juce::Colour cS = col::text, cI = col::text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThaMasterEditor)
};
} // namespace tmui

using ThaMasterEditor = tmui::ThaMasterEditor;
