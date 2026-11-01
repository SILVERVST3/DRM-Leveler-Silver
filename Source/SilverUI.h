#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// =============================================================================
//  Paleta SILVER (mesma familia do RefPro SILVER / SILVER-MB / VX_Leveler)
// =============================================================================
namespace silver
{
    const juce::Colour bgTop    { 0xffe1e4e9 };
    const juce::Colour bgBot    { 0xffb9bec6 };
    const juce::Colour panel    { 0xffeef0f3 };
    const juce::Colour panelDk  { 0xffd3d7dd };
    const juce::Colour edge     { 0xff98a0ab };
    const juce::Colour navy     { 0xff1b2a4a };
    const juce::Colour navySoft { 0xff4a5b7e };
    const juce::Colour orange   { 0xffef8a22 };
    const juce::Colour blue     { 0xff4aa3db };
    const juce::Colour display  { 0xff1d2431 };
    const juce::Colour grid     { 0xff2f394a };
    const juce::Colour lineLt   { 0xffd5dbe4 };
}

// =============================================================================
//  Fontes de parametro: simples (global) ou dupla (A/B conforme a VIEW)
// =============================================================================
struct ParamSource
{
    virtual ~ParamSource() = default;
    virtual float get() const = 0;
    virtual void  begin() = 0;
    virtual void  set (float plain) = 0;
    virtual void  end() = 0;
    virtual float def() const = 0;
    virtual juce::NormalisableRange<float> range() const = 0;
    virtual juce::String text (float plain) const = 0;

    void setOnce (float v) { begin(); set (v); end(); }
};

struct SingleSource : ParamSource
{
    juce::RangedAudioParameter* p;
    SingleSource (juce::AudioProcessorValueTreeState& s, const juce::String& pid) : p (s.getParameter (pid)) { jassert (p); }
    float get() const override { return p->convertFrom0to1 (p->getValue()); }
    void  begin() override { p->beginChangeGesture(); }
    void  set (float v) override { p->setValueNotifyingHost (p->convertTo0to1 (v)); }
    void  end() override { p->endChangeGesture(); }
    float def() const override { return p->convertFrom0to1 (p->getDefaultValue()); }
    juce::NormalisableRange<float> range() const override { return p->getNormalisableRange(); }
    juce::String text (float v) const override { return p->getText (p->convertTo0to1 (v), 32); }
};

// Edita A (LEFT/MID), B (RIGHT/SIDE) ou ambos (SUM / modo STEREO)
struct DualSource : ParamSource
{
    DRMLevelerAudioProcessor& proc;
    juce::RangedAudioParameter* a;
    juce::RangedAudioParameter* b;

    DualSource (DRMLevelerAudioProcessor& pr, const char* stem)
        : proc (pr), a (pr.apvts.getParameter (drmp::id (0, stem))), b (pr.apvts.getParameter (drmp::id (1, stem)))
    { jassert (a && b); }

    bool linked() const { return proc.isStereoLinked(); }
    bool editA() const  { return linked() || proc.getView() != 2; }
    bool editB() const  { return linked() || proc.getView() != 0; }
    juce::RangedAudioParameter* shown() const { return (! linked() && proc.getView() == 2) ? b : a; }

    float get() const override { auto* p = shown(); return p->convertFrom0to1 (p->getValue()); }
    void  begin() override { if (editA()) a->beginChangeGesture(); if (editB()) b->beginChangeGesture(); }
    void  set (float v) override
    {
        if (editA()) a->setValueNotifyingHost (a->convertTo0to1 (v));
        if (editB()) b->setValueNotifyingHost (b->convertTo0to1 (v));
    }
    void  end() override { if (editA()) a->endChangeGesture(); if (editB()) b->endChangeGesture(); }
    float def() const override { return a->convertFrom0to1 (a->getDefaultValue()); }
    juce::NormalisableRange<float> range() const override { return a->getNormalisableRange(); }
    juce::String text (float v) const override { return a->getText (a->convertTo0to1 (v), 32); }
    float getOther() const { auto* p = (shown() == a) ? b : a; return p->convertFrom0to1 (p->getValue()); }
};

inline juce::NormalisableRange<double> toDoubleRange (juce::NormalisableRange<float> r)
{
    return juce::NormalisableRange<double> (
        r.start, r.end,
        [r] (double, double, double v) { return (double) r.convertFrom0to1 ((float) v); },
        [r] (double, double, double x) { return (double) r.convertTo0to1 ((float) x); },
        [r] (double, double, double x) { return (double) r.snapToLegalValue ((float) x); });
}

// =============================================================================
//  Slider ligado a um ParamSource
// =============================================================================
class BoundSlider : public juce::Slider
{
public:
    BoundSlider (std::unique_ptr<ParamSource> s, SliderStyle style = RotaryHorizontalVerticalDrag)
        : juce::Slider (style, NoTextBox), src (std::move (s))
    {
        setNormalisableRange (toDoubleRange (src->range()));
        setDoubleClickReturnValue (true, src->def());
        setValue (src->get(), juce::dontSendNotification);
        setPopupDisplayEnabled (false, false, nullptr);
        setVelocityBasedMode (false);
        setMouseDragSensitivity (220);

        onDragStart = [this] { dragging = true; last = (float) getValue(); src->begin(); };
        onDragEnd   = [this] { dragging = false; src->end(); };
        onValueChange = [this]
        {
            if (updating) return;
            const float v = (float) getValue();
            const float old = last;
            if (dragging) src->set (v); else src->setOnce (v);
            last = v;
            if (onUserChange) onUserChange (old, v);
        };
    }

    void refresh()
    {
        if (dragging) return;
        const float v = src->get();
        if (std::abs ((float) getValue() - v) > 1.0e-6f)
        {
            updating = true; setValue (v, juce::dontSendNotification); updating = false;
        }
        last = v;
    }

    juce::String getTextFromValue (double v) override { return src->text ((float) v); }
    ParamSource& source() { return *src; }

    std::function<void (float oldV, float newV)> onUserChange;

private:
    std::unique_ptr<ParamSource> src;
    bool dragging = false, updating = false;
    float last = 0;
};

// Botao toggle ligado a parametro booleano (ou choice: valor "onValue")
class BoundToggle : public juce::TextButton
{
public:
    BoundToggle (const juce::String& txt, std::unique_ptr<ParamSource> s, float onV = 1.0f, bool radio = false)
        : juce::TextButton (txt), src (std::move (s)), onValue (onV), isRadio (radio)
    {
        setClickingTogglesState (false);
        onClick = [this]
        {
            if (isRadio) src->setOnce (onValue);
            else src->setOnce (isOn() ? 0.0f : onValue);
        };
    }
    bool isOn() const { return std::abs (src->get() - onValue) < 0.5f; }
    void refresh() { setToggleState (isOn(), juce::dontSendNotification); }
private:
    std::unique_ptr<ParamSource> src;
    float onValue; bool isRadio;
};

// =============================================================================
//  Medidor vertical
// =============================================================================
class LevelMeter : public juce::Component
{
public:
    void push (float linear)
    {
        const float db = juce::jlimit (-70.0f, 12.0f, juce::Decibels::gainToDecibels (linear, -70.0f));
        levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.2f);
        if (db >= peakDb) { peakDb = db; holdTicks = 45; }
        else if (--holdTicks < 0) peakDb = juce::jmax (-70.0f, peakDb - 0.8f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (silver::display);
        g.fillRoundedRectangle (r, 2.0f);
        auto inner = r.reduced (2.0f);
        const float yLvl = yFor (levelDb, inner);
        juce::ColourGradient cg (juce::Colour (0xffef4b3a), inner.getX(), yFor (0.0f, inner),
                                 juce::Colour (0xff3d7fb8), inner.getX(), inner.getBottom(), false);
        cg.addColour (0.10, silver::orange);
        cg.addColour (0.30, juce::Colour (0xff7cc6e8));
        g.setGradientFill (cg);
        g.fillRect (inner.withTop (yLvl));
        g.setColour (peakDb > 0.0f ? juce::Colour (0xffef4b3a) : silver::lineLt);
        g.fillRect (inner.getX(), yFor (peakDb, inner) - 1.0f, inner.getWidth(), 2.0f);
        g.setColour (silver::edge);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }

    static float yFor (float db, juce::Rectangle<float> r)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (6.0f - db) / 66.0f);
        return r.getY() + t * r.getHeight();
    }

private:
    float levelDb = -70.0f, peakDb = -70.0f;
    int holdTicks = 0;
};

// =============================================================================
//  LookAndFeel
// =============================================================================
class SilverLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SilverLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, silver::panel);
        setColour (juce::ComboBox::textColourId, silver::navy);
        setColour (juce::ComboBox::outlineColourId, silver::edge);
        setColour (juce::ComboBox::arrowColourId, silver::navy);
        setColour (juce::PopupMenu::backgroundColourId, silver::panel);
        setColour (juce::PopupMenu::textColourId, silver::navy);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, silver::navy);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Label::textColourId, silver::navy);
    }

    juce::Font font (float h, bool bold = true) const
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto sq = bounds.withSizeKeepingCentre (size, size).reduced (size * 0.04f);
        const auto c = sq.getCentre();
        const float r = sq.getWidth() * 0.5f;
        const float ang = a0 + pos * (a1 - a0);
        const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
        const bool ring = s.getProperties().contains ("ring");
        const float trackR = ring ? r * 0.80f : r * 0.90f;
        const float tw = juce::jmax (2.5f, r * (ring ? 0.07f : 0.10f));

        // trilho
        juce::Path track;
        track.addCentredArc (c.x, c.y, trackR, trackR, 0, a0, a1, true);
        g.setColour (silver::panelDk.darker (0.15f));
        g.strokePath (track, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // valor
        const float mid = (a0 + a1) * 0.5f;
        juce::Path val;
        if (bipolar)
        {
            const float z = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);  // posicao do zero
            val.addCentredArc (c.x, c.y, trackR, trackR, 0, juce::jmin (z, ang), juce::jmax (z, ang), true);
        }
        else         val.addCentredArc (c.x, c.y, trackR, trackR, 0, a0, ang, true);
        g.setColour (silver::navy);
        g.strokePath (val, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // anel de ganho (Compression): laranja = boost, azul = reducao
        if (ring)
        {
            const float gdb = (float) (double) s.getProperties()["ring"];
            const float rr = r * 0.96f;
            juce::Path bg;
            bg.addCentredArc (c.x, c.y, rr, rr, 0, a0, a1, true);
            g.setColour (silver::display.withAlpha (0.18f));
            g.strokePath (bg, juce::PathStrokeType (r * 0.07f));
            const float span = juce::jlimit (-1.0f, 1.0f, gdb / 24.0f) * (a1 - a0) * 0.5f;
            if (std::abs (span) > 0.002f)
            {
                juce::Path gp;
                gp.addCentredArc (c.x, c.y, rr, rr, 0, juce::jmin (mid, mid + span), juce::jmax (mid, mid + span), true);
                g.setColour (gdb > 0 ? silver::orange : silver::blue);
                g.strokePath (gp, juce::PathStrokeType (r * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // corpo metalico
        const float kr = r * (ring ? 0.62f : 0.70f);
        auto body = juce::Rectangle<float> (kr * 2, kr * 2).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillEllipse (body.translated (0, kr * 0.08f).expanded (1.0f));
        juce::ColourGradient kg (juce::Colours::white, c.x - kr * 0.5f, c.y - kr * 0.7f,
                                 juce::Colour (0xffaeb4bd), c.x + kr * 0.4f, c.y + kr, true);
        g.setGradientFill (kg);
        g.fillEllipse (body);
        g.setColour (silver::edge);
        g.drawEllipse (body, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawEllipse (body.reduced (kr * 0.12f), 0.8f);

        // ponteiro
        juce::Path ptr;
        const float pw = juce::jmax (2.0f, kr * 0.10f);
        ptr.addRoundedRectangle (-pw * 0.5f, -kr * 0.92f, pw, kr * 0.45f, pw * 0.5f);
        g.setColour (silver::navy);
        g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (on)
        {
            juce::ColourGradient cg (silver::navySoft, r.getX(), r.getY(), silver::navy, r.getX(), r.getBottom(), false);
            g.setGradientFill (cg);
        }
        else
        {
            juce::ColourGradient cg (juce::Colours::white, r.getX(), r.getY(), silver::panelDk, r.getX(), r.getBottom(), false);
            g.setGradientFill (cg);
        }
        g.fillRoundedRectangle (r, 3.0f);
        if (over || down)
        {
            g.setColour ((on ? juce::Colours::white : silver::navy).withAlpha (down ? 0.15f : 0.07f));
            g.fillRoundedRectangle (r, 3.0f);
        }
        g.setColour (on ? silver::navy : silver::edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (font (juce::jmin (12.5f, b.getHeight() * 0.55f)));
        auto col = b.getToggleState() ? juce::Colours::white : silver::navy;
        if (! b.isEnabled()) col = col.withAlpha (0.35f);
        g.setColour (col);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, 1);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        if (style == juce::Slider::TwoValueHorizontal)
        {
            auto track = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
            g.setColour (silver::display);
            g.fillRoundedRectangle (track, 3.0f);
            const bool bs = s.getProperties().getWithDefault ("bandStop", false);
            const bool on = s.getProperties().getWithDefault ("active", false);
            auto hl = on ? silver::orange : silver::navySoft.withAlpha (0.6f);
            g.setColour (hl);
            if (bs)
            {
                g.fillRoundedRectangle (track.withRight (minPos), 3.0f);
                g.fillRoundedRectangle (track.withLeft (maxPos), 3.0f);
            }
            else g.fillRect (track.withLeft (minPos).withRight (maxPos));
            for (float p : { minPos, maxPos })
            {
                auto th = juce::Rectangle<float> (12.0f, 16.0f).withCentre ({ p, r.getCentreY() });
                juce::ColourGradient cg (juce::Colours::white, th.getX(), th.getY(), juce::Colour (0xffaeb4bd), th.getX(), th.getBottom(), false);
                g.setGradientFill (cg);
                g.fillEllipse (th.withSizeKeepingCentre (14.0f, 14.0f));
                g.setColour (silver::navy);
                g.drawEllipse (th.withSizeKeepingCentre (14.0f, 14.0f), 1.2f);
            }
            return;
        }

        if (style == juce::Slider::LinearVertical)
        {
            auto groove = r.withSizeKeepingCentre (4.0f, r.getHeight());
            g.setColour (silver::display.withAlpha (0.75f));
            g.fillRoundedRectangle (groove, 2.0f);
            auto th = juce::Rectangle<float> (r.getWidth(), 10.0f).withCentre ({ r.getCentreX(), pos });
            juce::ColourGradient cg (juce::Colours::white, th.getX(), th.getY(), juce::Colour (0xffa9b0ba), th.getX(), th.getBottom(), false);
            g.setGradientFill (cg);
            g.fillRoundedRectangle (th, 2.0f);
            g.setColour (silver::navy);
            g.drawRoundedRectangle (th, 2.0f, 1.0f);
            g.drawHorizontalLine ((int) th.getCentreY(), th.getX() + 2, th.getRight() - 2);
            return;
        }
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f, false); }
    juce::Font getPopupMenuFont() override { return font (14.0f, false); }
};
