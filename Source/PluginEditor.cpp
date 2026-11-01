#include "PluginEditor.h"
#include "Presets.h"

using namespace drmp;

namespace
{
    std::unique_ptr<ParamSource> dual (DRMLevelerAudioProcessor& p, const char* stem)
    {
        return std::make_unique<DualSource> (p, stem);
    }
    std::unique_ptr<ParamSource> single (DRMLevelerAudioProcessor& p, const char* pid)
    {
        return std::make_unique<SingleSource> (p.apvts, pid);
    }
    juce::String freqText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (f)) + " Hz";
    }
}

DRMLevelerAudioProcessorEditor::DRMLevelerAudioProcessorEditor (DRMLevelerAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      hiS (p, hiThr), loS (p, loThr), tgS (p, target), gateS (p, gate),
      display (p, hiS, loS, tgS, gateS),
      kSens   ("SENSITIVITY",   dual (p, sens)),
      kRetrig ("MIN RETRIGGER", dual (p, retrig)),
      kRange  ("GAIN RANGE",    dual (p, range)),
      kComp   ("COMPRESSION",   dual (p, comp), true),
      kHold   ("HOLD",          dual (p, hold)),
      kRec    ("RECOVERY",      dual (p, recovery)),
      kGate   ("GATE RANGE",    dual (p, gate), true),
      modeDual   ("DUAL MONO", single (p, mode), 0.0f, true),
      modeStereo ("STEREO",    single (p, mode), 1.0f, true),
      modeMS     ("M/S",       single (p, mode), 2.0f, true),
      bypassBtn  ("BYPASS",    single (p, bypass)),
      inFader0  (single (p, inL),  juce::Slider::LinearVertical),
      inFader1  (single (p, inR),  juce::Slider::LinearVertical),
      outFader0 (single (p, outL), juce::Slider::LinearVertical),
      outFader1 (single (p, outR), juce::Slider::LinearVertical),
      inLinkBtn  ("LINK", single (p, inLink)),
      outLinkBtn ("LINK", single (p, outLink)),
      scfOnBtn ("SCF",       dual (p, scfOn)),
      bpBtn    ("BAND-PASS", dual (p, scfMode), 0.0f, true),
      bsBtn    ("BAND-STOP", dual (p, scfMode), 1.0f, true),
      soloBtn  ("S",         dual (p, solo)),
      scBtn    ("SC",        dual (p, sc)),
      softClipBtn ("SOFT CLIP", single (p, softClip)),
      sCurveBtn   ("S-CURVE",   single (p, sCurve)),
      scfLoS (p, scfLo), scfHiS (p, scfHi), scfModeS (p, scfMode), scfOnS (p, scfOn)
{
    setLookAndFeel (&laf);
    content.setLookAndFeel (&laf);
    content.painter = [this] (juce::Graphics& g) { paintContent (g); };
    addAndMakeVisible (content);

    content.addAndMakeVisible (display);
    for (auto* k : { &kSens, &kRetrig, &kRange, &kComp, &kHold, &kRec, &kGate })
        content.addAndMakeVisible (*k);
    kComp.slider.getProperties().set ("ring", 0.0);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &modeDual, &modeStereo, &modeMS, &btnA, &btnCopy, &btnB, &viewL, &viewSum, &viewR,
             &presetBox, &bypassBtn, &inFader0, &inFader1, &outFader0, &outFader1, &inLinkBtn, &outLinkBtn,
             &scfOnBtn, &bpBtn, &bsBtn, &soloBtn, &scBtn, &softClipBtn, &sCurveBtn, &scfRange,
             &scfLoLbl, &scfHiLbl })
        content.addAndMakeVisible (c);
    for (int c = 0; c < 2; ++c)
    {
        content.addAndMakeVisible (inMeter[c]);
        content.addAndMakeVisible (outMeter[c]);
        for (auto* l : { &inVal[c], &outVal[c] })
        {
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
            l->setColour (juce::Label::textColourId, silver::navy);
            content.addAndMakeVisible (*l);
        }
    }
    for (auto* l : { &scfLoLbl, &scfHiLbl })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        l->setColour (juce::Label::textColourId, silver::navy);
    }

    // ---- links de entrada/saida (offset relativo fixo) ----
    auto linkPair = [] (BoundSlider& a, BoundSlider& b, BoundToggle& link)
    {
        a.onUserChange = [&b, &link] (float o, float n)
        {
            if (! link.isOn()) return;
            auto& s = b.source(); auto rr = s.range();
            s.setOnce (juce::jlimit (rr.start, rr.end, s.get() + (n - o)));
        };
    };
    linkPair (inFader0, inFader1, inLinkBtn);
    linkPair (inFader1, inFader0, inLinkBtn);
    linkPair (outFader0, outFader1, outLinkBtn);
    linkPair (outFader1, outFader0, outLinkBtn);

    // ---- A/B ----
    btnA.onClick = [this] { proc.switchAB (0); refreshAll(); };
    btnB.onClick = [this] { proc.switchAB (1); refreshAll(); };
    btnCopy.onClick = [this] { proc.copyToOtherSlot(); };
    btnCopy.setTooltip ("Copiar o estado atual para o outro slot");

    // ---- view ----
    viewL.onClick   = [this] { proc.setView (0); refreshAll(); display.repaint(); };
    viewSum.onClick = [this] { proc.setView (1); refreshAll(); display.repaint(); };
    viewR.onClick   = [this] { proc.setView (2); refreshAll(); display.repaint(); };

    // ---- presets ----
    const auto& pl = drmpresets::list();
    for (int i = 0; i < (int) pl.size(); ++i) presetBox.addItem (pl[(size_t) i].name, i + 1);
    presetBox.setSelectedId (proc.currentPreset + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx < 0) return;
        proc.currentPreset = idx;
        drmpresets::apply (proc.apvts, idx);
        refreshAll();
    };

    // ---- side-chain filter (2 alcas) ----
    scfRange.setNormalisableRange (toDoubleRange (scfLoS.range()));
    scfRange.onDragStart = [this] { scfDragging = true; scfLoS.begin(); scfHiS.begin(); };
    scfRange.onDragEnd   = [this] { scfDragging = false; scfLoS.end(); scfHiS.end(); };
    scfRange.onValueChange = [this]
    {
        if (scfUpdating) return;
        const float lo = (float) scfRange.getMinValue(), hi = (float) scfRange.getMaxValue();
        if (scfDragging) { scfLoS.set (lo); scfHiS.set (hi); }
        else { scfLoS.setOnce (lo); scfHiS.setOnce (hi); }
    };
    scfRange.setMouseDragSensitivity (300);

    soloBtn.setTooltip ("Solo: ouvir o sinal de deteccao (apos o SCF)");
    scBtn.setTooltip ("Usar a entrada externa de side-chain como chave de deteccao");
    sCurveBtn.setTooltip ("Rampas de ataque/recovery em curva S");
    softClipBtn.setTooltip ("Arredonda picos acima de 0 dBFS");

    constrainer = std::make_unique<juce::ComponentBoundsConstrainer>();
    constrainer->setFixedAspectRatio ((double) baseW / baseH);
    constrainer->setSizeLimits ((int) (baseW * 0.75), (int) (baseH * 0.75), (int) (baseW * 1.8), (int) (baseH * 1.8));
    setConstrainer (constrainer.get());
    setResizable (true, true);

    content.setBounds (0, 0, baseW, baseH);
    layout();
    refreshAll();
    setSize (baseW, baseH);
    startTimerHz (30);
}

DRMLevelerAudioProcessorEditor::~DRMLevelerAudioProcessorEditor()
{
    stopTimer();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void DRMLevelerAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content.setTransform (juce::AffineTransform::scale (s));
}

void DRMLevelerAudioProcessorEditor::layout()
{
    // cabecalho
    modeDual.setBounds   (232, 20, 82, 22);
    modeStereo.setBounds (316, 20, 70, 22);
    modeMS.setBounds     (388, 20, 50, 22);
    btnA.setBounds    (458, 20, 28, 22);
    btnCopy.setBounds (488, 20, 34, 22);
    btnB.setBounds    (524, 20, 28, 22);
    viewL.setBounds   (574, 20, 64, 22);
    viewSum.setBounds (640, 20, 64, 22);
    viewR.setBounds   (706, 20, 64, 22);
    presetBox.setBounds (800, 18, 178, 26);
    bypassBtn.setBounds (986, 20, 64, 22);

    // medidores
    auto meterSection = [] (int x0, BoundSlider& f0, BoundSlider& f1, LevelMeter* m, juce::Label* v, BoundToggle& link)
    {
        v[0].setBounds (x0 + 2, 74, 52, 16);
        v[1].setBounds (x0 + 54, 74, 52, 16);
        f0.setBounds (x0 + 6, 96, 18, 440);
        m[0].setBounds (x0 + 30, 98, 20, 436);
        m[1].setBounds (x0 + 58, 98, 20, 436);
        f1.setBounds (x0 + 84, 96, 18, 440);
        link.setBounds (x0 + 30, 552, 48, 22);
    };
    meterSection (10,  inFader0,  inFader1,  inMeter,  inVal,  inLinkBtn);
    meterSection (942, outFader0, outFader1, outMeter, outVal, outLinkBtn);

    // display e knobs
    display.setBounds (128, 54, 804, 278);
    const int ky = 344, kh = 160;
    int x = 128;
    for (auto* k : { &kSens, &kRetrig, &kRange })  { k->setBounds (x, ky + 22, 104, kh - 30); x += 104; }
    kComp.setBounds (x, ky, 180, kh); x += 180;
    for (auto* k : { &kHold, &kRec, &kGate })      { k->setBounds (x, ky + 22, 104, kh - 30); x += 104; }

    // barra SCF
    const int by = 546, bh = 26;
    scfOnBtn.setBounds (138, by, 50, bh);
    bpBtn.setBounds    (194, by, 82, bh);
    bsBtn.setBounds    (280, by, 82, bh);
    scfLoLbl.setBounds (366, by, 58, bh);
    scfRange.setBounds (426, by - 2, 222, bh + 4);
    scfHiLbl.setBounds (650, by, 62, bh);
    soloBtn.setBounds  (718, by, 28, bh);
    scBtn.setBounds    (750, by, 36, bh);
    softClipBtn.setBounds (800, by, 72, bh);
    sCurveBtn.setBounds   (876, by, 50, bh);
}

void DRMLevelerAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    auto r = content.getLocalBounds().toFloat();
    juce::ColourGradient bg (silver::bgTop, 0, 0, silver::bgBot, 0, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // faixa do cabecalho
    juce::ColourGradient hg (juce::Colours::white.withAlpha (0.7f), 0, 0, silver::panelDk, 0, 48, false);
    g.setGradientFill (hg);
    g.fillRect (0.0f, 0.0f, r.getWidth(), 48.0f);
    g.setColour (silver::edge);
    g.drawHorizontalLine (48, 0, r.getWidth());

    // logo
    g.setColour (silver::navy);
    g.setFont (juce::Font (juce::FontOptions (23.0f, juce::Font::bold)));
    g.drawText ("DRM_Leveler", 16, 4, 200, 26, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.setColour (silver::navySoft);
    g.drawText ("S  I  L  V  E  R", 18, 29, 200, 14, juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.setColour (silver::navySoft);
    g.drawText ("PROCESS", 232, 5, 120, 12, juce::Justification::centredLeft);
    g.drawText ("A / B", 458, 5, 94, 12, juce::Justification::centredLeft);
    g.drawText ("VIEW", 574, 5, 120, 12, juce::Justification::centredLeft);
    g.drawText ("PRESET", 800, 5, 120, 12, juce::Justification::centredLeft);

    auto panelAt = [&] (juce::Rectangle<float> p)
    {
        juce::ColourGradient pg (silver::panel, p.getX(), p.getY(), silver::panelDk, p.getX(), p.getBottom(), false);
        g.setGradientFill (pg);
        g.fillRoundedRectangle (p, 6.0f);
        g.setColour (silver::edge);
        g.drawRoundedRectangle (p, 6.0f, 1.0f);
    };
    panelAt ({ 10, 54, 108, 534 });
    panelAt ({ 942, 54, 108, 534 });
    panelAt ({ 128, 338, 804, 172 });
    panelAt ({ 128, 518, 804, 70 });

    g.setColour (silver::navy);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("INPUT", 10, 58, 108, 14, juce::Justification::centred);
    g.drawText ("OUTPUT", 942, 58, 108, 14, juce::Justification::centred);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    for (int x0 : { 10, 942 })
    {
        g.drawText ("L", x0 + 4, 552, 22, 22, juce::Justification::centred);
        g.drawText ("R", x0 + 82, 552, 22, 22, juce::Justification::centred);
    }

    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.setColour (silver::navySoft);
    g.drawText ("SIDE-CHAIN FILTER / DETECTION", 138, 524, 300, 14, juce::Justification::centredLeft);
    g.drawText ("OUTPUT", 800, 524, 120, 14, juce::Justification::centredLeft);
}

void DRMLevelerAudioProcessorEditor::refreshAll()
{
    for (auto* k : { &kSens, &kRetrig, &kRange, &kComp, &kHold, &kRec, &kGate }) k->refresh();
    for (auto* b : { &modeDual, &modeStereo, &modeMS, &bypassBtn, &inLinkBtn, &outLinkBtn,
                     &scfOnBtn, &bpBtn, &bsBtn, &soloBtn, &scBtn, &softClipBtn, &sCurveBtn })
        b->refresh();
    for (auto* f : { &inFader0, &inFader1, &outFader0, &outFader1 }) f->refresh();

    inVal[0].setText (juce::String (inFader0.source().get(), 1), juce::dontSendNotification);
    inVal[1].setText (juce::String (inFader1.source().get(), 1), juce::dontSendNotification);
    outVal[0].setText (juce::String (outFader0.source().get(), 1), juce::dontSendNotification);
    outVal[1].setText (juce::String (outFader1.source().get(), 1), juce::dontSendNotification);

    // view / modo
    const bool mono = proc.isMonoLayout();
    const bool linked = proc.isStereoLinked();
    const bool ms = proc.getMode() == MidSide;
    const int view = linked ? 1 : proc.getView();
    viewL.setButtonText (ms ? "MID" : "LEFT");
    viewR.setButtonText (ms ? "SIDE" : "RIGHT");
    viewL.setToggleState (view == 0, juce::dontSendNotification);
    viewSum.setToggleState (view == 1, juce::dontSendNotification);
    viewR.setToggleState (view == 2, juce::dontSendNotification);
    viewL.setEnabled (! linked);
    viewR.setEnabled (! linked);
    for (auto* b : { &modeDual, &modeStereo, &modeMS }) b->setEnabled (! mono);

    btnA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    btnB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    btnCopy.setButtonText (proc.getABSlot() == 0 ? "A>B" : "B>A");

    scBtn.setAlpha (proc.hasSidechain() ? 1.0f : 0.45f);

    // SCF
    if (! scfDragging)
    {
        scfUpdating = true;
        scfRange.setMinAndMaxValues (scfLoS.get(), scfHiS.get(), juce::dontSendNotification);
        scfUpdating = false;
    }
    const bool bs = scfModeS.get() > 0.5f, on = scfOnS.get() > 0.5f;
    if ((bool) scfRange.getProperties().getWithDefault ("bandStop", false) != bs
        || (bool) scfRange.getProperties().getWithDefault ("active", false) != on)
    {
        scfRange.getProperties().set ("bandStop", bs);
        scfRange.getProperties().set ("active", on);
        scfRange.repaint();
    }
    scfLoLbl.setText (freqText ((float) scfRange.getMinValue()), juce::dontSendNotification);
    scfHiLbl.setText (freqText ((float) scfRange.getMaxValue()), juce::dontSendNotification);

    if (presetBox.getSelectedId() != proc.currentPreset + 1)
        presetBox.setSelectedId (proc.currentPreset + 1, juce::dontSendNotification);
}

void DRMLevelerAudioProcessorEditor::timerCallback()
{
    DisplayColumn buf[256];
    for (int guard = 0; guard < 8; ++guard)
    {
        const int n = proc.readColumns (buf, 256);
        if (n <= 0) break;
        display.push (buf, n);
    }

    for (int c = 0; c < 2; ++c)
    {
        inMeter[c].push (proc.meterIn[c].exchange (0.0f));
        outMeter[c].push (proc.meterOut[c].exchange (0.0f));
    }

    const bool linked = proc.isStereoLinked();
    const int view = linked ? 1 : proc.getView();
    float gdb;
    if (view == 1)
    {
        const float g0 = proc.gainNow[0].load(), g1 = proc.gainNow[1].load();
        gdb = std::abs (g0) >= std::abs (g1) ? g0 : g1;
    }
    else gdb = proc.gainNow[view == 0 ? 0 : 1].load();
    const double old = (double) kComp.slider.getProperties()["ring"];
    if (std::abs (old - gdb) > 0.02)
    {
        kComp.slider.getProperties().set ("ring", (double) gdb);
        kComp.slider.repaint();
    }

    refreshAll();
    display.repaint();
}
