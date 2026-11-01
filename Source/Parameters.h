#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// =============================================================================
//  DRM_Leveler SILVER - parametros
//  Cada processador (A = Left/Mid, B = Right/Side) tem seu proprio conjunto de
//  parametros de deteccao/ganho. No modo STEREO apenas o conjunto A e usado.
// =============================================================================
namespace drmp
{
    // ---- stems por canal (prefixo "a_" ou "b_") ----
    static constexpr const char* hiThr    = "hiThr";
    static constexpr const char* loThr    = "loThr";
    static constexpr const char* target   = "target";
    static constexpr const char* comp     = "comp";
    static constexpr const char* range    = "range";
    static constexpr const char* retrig   = "retrig";
    static constexpr const char* hold     = "hold";
    static constexpr const char* recovery = "recovery";
    static constexpr const char* sens     = "sens";
    static constexpr const char* gate     = "gate";
    static constexpr const char* scfOn    = "scfOn";
    static constexpr const char* scfMode  = "scfMode";
    static constexpr const char* scfLo    = "scfLo";
    static constexpr const char* scfHi    = "scfHi";
    static constexpr const char* solo     = "solo";
    static constexpr const char* sc       = "sc";

    // ---- globais ----
    static constexpr const char* mode     = "mode";      // 0 Dual Mono, 1 Stereo, 2 M/S
    static constexpr const char* inL      = "inL";
    static constexpr const char* inR      = "inR";
    static constexpr const char* outL     = "outL";
    static constexpr const char* outR     = "outR";
    static constexpr const char* inLink   = "inLink";
    static constexpr const char* outLink  = "outLink";
    static constexpr const char* softClip = "softClip";
    static constexpr const char* sCurve   = "sCurve";
    static constexpr const char* bypass   = "bypass";

    enum Mode { DualMono = 0, Stereo = 1, MidSide = 2 };

    inline juce::String id (int ch, const char* stem)
    {
        return juce::String (ch == 0 ? "a_" : "b_") + stem;
    }

    inline juce::NormalisableRange<float> skewed (float lo, float hi, float mid, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (mid);
        return r;
    }

    inline juce::NormalisableRange<float> freqRange()
    {
        // mapeamento logaritmico 20 Hz .. 20 kHz
        return juce::NormalisableRange<float> (
            20.0f, 20000.0f,
            [] (float s, float e, float v) { return s * std::pow (e / s, v); },
            [] (float s, float e, float f) { return std::log (f / s) / std::log (e / s); },
            [] (float, float, float f) { return std::round (f); });
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto dB = [] (float v, int) { return String (v, 1) + " dB"; };
        auto ms = [] (float v, int) { return String (juce::roundToInt (v)) + " ms"; };
        auto pc = [] (float v, int) { return String (v, 1) + " %"; };
        auto hz = [] (float v, int)
        {
            return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (juce::roundToInt (v)) + " Hz";
        };

        for (int ch = 0; ch < 2; ++ch)
        {
            const String tag = ch == 0 ? " (L/M)" : " (R/S)";
            auto fp = [&] (const char* stem, const String& name, NormalisableRange<float> r, float def,
                           std::function<String (float, int)> fmt)
            {
                layout.add (std::make_unique<AudioParameterFloat> (
                    ParameterID { id (ch, stem), 1 }, name + tag, r, def,
                    AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
            };

            fp (hiThr,    "High Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f),  0.0f, dB);
            fp (loThr,    "Low Threshold",  NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -40.0f, dB);
            fp (target,   "Target Level",   NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -12.0f, dB);
            fp (comp,     "Compression",    NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 50.0f, pc);
            fp (range,    "Gain Range",     skewed (0.0f, 80.0f, 12.0f, 0.1f), 24.0f, dB);
            fp (retrig,   "Min Retrigger",  skewed (5.0f, 500.0f, 60.0f, 1.0f), 23.0f, ms);
            fp (hold,     "Hold",           skewed (0.0f, 1000.0f, 120.0f, 1.0f), 85.0f, ms);
            fp (recovery, "Recovery",       skewed (0.0f, 3000.0f, 400.0f, 1.0f), 630.0f, ms);
            fp (sens,     "Sensitivity",    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pc);
            fp (gate,     "Gate Range",     NormalisableRange<float> (-80.0f, 24.0f, 0.1f), 0.0f, dB);

            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id (ch, scfOn), 1 }, "SC Filter" + tag, false));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id (ch, scfMode), 1 }, "SC Filter Mode" + tag,
                                                                StringArray { "Band-Pass", "Band-Stop" }, 0));
            fp (scfLo, "SC Filter Low",  freqRange(), 80.0f, hz);
            fp (scfHi, "SC Filter High", freqRange(), 20000.0f, hz);
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id (ch, solo), 1 }, "SC Solo" + tag, false));
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id (ch, sc), 1 }, "External Side-Chain" + tag, false));
        }

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { mode, 1 }, "Process Mode",
                                                            StringArray { "Dual Mono", "Stereo", "M/S" }, 1));
        auto gainP = [&] (const char* pid, const String& name)
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { pid, 1 }, name, NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
        };
        gainP (inL, "Input L");
        gainP (inR, "Input R");
        gainP (outL, "Output L");
        gainP (outR, "Output R");
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { inLink, 1 }, "Input Link", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { outLink, 1 }, "Output Link", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { softClip, 1 }, "Soft Clipper", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { sCurve, 1 }, "S-Curve", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));
        return layout;
    }
}
