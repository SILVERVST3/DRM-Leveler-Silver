#pragma once
#include "PluginProcessor.h"
#include "SilverUI.h"
#include "WaveDisplay.h"

// Bloco de knob: nome em cima, valor embaixo
class KnobBlock : public juce::Component
{
public:
    KnobBlock (const juce::String& n, std::unique_ptr<ParamSource> src, bool bipolar = false)
        : name (n), slider (std::move (src))
    {
        if (bipolar) slider.getProperties().set ("bipolar", true);
        addAndMakeVisible (slider);
        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setEditable (false, true);
        valueLabel.setTooltip ("Double-click to type a value. Shift-drag for fine adjustment.");
        valueLabel.setColour (juce::Label::textColourId, silver::navy);
        valueLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        valueLabel.onTextChange = [this]
        {
            const auto text = valueLabel.getText().trim();
            if (! text.containsAnyOf ("0123456789")) { lastText.clear(); refresh(); return; }
            slider.setValue (text.getFloatValue(), juce::sendNotificationSync);
            lastText.clear(); refresh();
        };
        addAndMakeVisible (valueLabel);
        slider.setTitle (n); slider.setTooltip (n + ": Shift-drag for fine adjustment; double-click to reset.");
    }

    void refresh()
    {
        slider.refresh();
        const auto t = slider.getTextFromValue (slider.getValue());
        if (t != lastText && ! valueLabel.isBeingEdited()) { lastText = t; valueLabel.setText (t, juce::dontSendNotification); repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (silver::navy);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText (name, getLocalBounds().removeFromTop (16), juce::Justification::centred);

    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (16);
        valueLabel.setBounds (r.removeFromBottom (20));
        slider.setBounds (r);
    }

    juce::String name;
    BoundSlider slider;
    juce::String lastText;
    juce::Label valueLabel;
};

class DRMLevelerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DRMLevelerAudioProcessorEditor (DRMLevelerAudioProcessor&);
    ~DRMLevelerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int baseW = 1060, baseH = 760;

private:
    void timerCallback() override;
    void layout();
    void paintContent (juce::Graphics&);
    void refreshAll();

    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    SilverLookAndFeel laf;
    DRMLevelerAudioProcessor& proc;
    Content content;
    juce::TooltipWindow tooltipWindow { this, 700 };

    // fontes usadas pelo display
    DualSource hiS, loS, tgS, gateS;
    WaveDisplay display;

    KnobBlock kSens, kRetrig, kRange, kComp, kHold, kRec, kGate;
    KnobBlock kBoost, kCut, kMix, kCeiling;
    BoundToggle measureMainBtn, adaptiveBtn, osBtn;
    juce::TextButton pauseBtn { "PAUSE" }, zoomOutBtn { "-" }, zoomInBtn { "+" };
    juce::TextButton saveBtn { "SAVE" }, loadBtn { "LOAD" };
    juce::Label timeLabel, presetStatus, sidechainStatus;
    std::unique_ptr<juce::FileChooser> fileChooser;
    void choosePresetFile (bool save);

    // cabecalho
    BoundToggle modeDual, modeStereo, modeMS;
    juce::TextButton btnA { "A" }, btnCopy { ">" }, btnB { "B" };
    juce::TextButton viewL { "LEFT" }, viewSum { "BOTH" }, viewR { "RIGHT" };
    juce::ComboBox presetBox;
    BoundToggle bypassBtn;

    // medidores / faders
    LevelMeter inMeter[2], outMeter[2];
    BoundSlider inFader0, inFader1, outFader0, outFader1;
    BoundToggle inLinkBtn, outLinkBtn;
    BoundToggle inCh0, inCh1, outCh0, outCh1;   // liga/desliga de canal (L/R ou M/S)
    juce::Label inVal[2], outVal[2];

    // side-chain filter
    BoundToggle scfOnBtn, bpBtn, bsBtn, soloBtn, scBtn, softClipBtn, sCurveBtn;
    DualSource scfLoS, scfHiS, scfModeS, scfOnS;
    juce::Slider scfRange { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    bool scfDragging = false, scfUpdating = false;
    juce::Label scfLoLbl, scfHiLbl;

    std::unique_ptr<juce::ComponentBoundsConstrainer> constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DRMLevelerAudioProcessorEditor)
};
